/**
 * @file
 * @brief Private spdlog backend ownership, flush acknowledgement and admission state.
 */

#pragma once

#include <atlas/diagnostics/logging.hpp>
#include <spdlog/async_logger.h>
#include <spdlog/details/thread_pool.h>
#include <spdlog/sinks/base_sink.h>

#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>

namespace atlas::diagnostics::detail
{
/// @brief Guard rejecting recursive logger entry while an output callback is executing.
inline thread_local bool inside_output = false;

/// @brief Thread-local reentrancy guard for output callbacks and sink operations.
struct output_scope
{
    /// @brief Initialize thread-local reentrancy guard for output callbacks and sink operations.
    output_scope()
    {
        inside_output = true;
    }

    /// @brief Release output scope resources and complete its scoped cleanup.
    ~output_scope()
    {
        inside_output = false;
    }

    /// @brief Disable copy construction/assignment to preserve unique lifecycle ownership.
    output_scope(const output_scope&) = delete;
    /// @brief Disable copy construction/assignment to preserve unique lifecycle ownership.
    output_scope& operator=(const output_scope&) = delete;
};

/// @brief Shared backend counters and flush acknowledgement used without recursive error logging.
struct logging_counts
{
    /// @brief Submission timestamp or lifetime total of admitted work, as defined by the enclosing
    /// record.
    std::atomic<std::uint64_t> submitted{0}, filtered{0}, written{0}, errors{0};
    /// @brief Synchronization for the enclosing shared registry/capture/execution state.
    std::mutex mutex;
    /// @brief Condition variable acknowledging backend flush completion.
    std::condition_variable flushed;
    /// @brief Completed backend flush acknowledgements, including failed output flush attempts.
    std::uint64_t flush_count{0};
};

/// @brief Private spdlog sink serializing injected output and containing standard/nonstandard
/// exceptions.
class output_sink final : public spdlog::sinks::base_sink<std::mutex>
{
  public:
    /// @brief Bind serialized output and validate delegated rotation support before accepting
    /// messages.
    output_sink(std::shared_ptr<log_output> output, std::shared_ptr<logging_counts> counts,
                logging_options options);

  private:
    /// @brief Format and deliver a record under the sink lock, containing all output exceptions.
    void sink_it_(const spdlog::details::log_msg& message) override;
    /// @brief Flush the injected output and always acknowledge completion, including output
    /// failures.
    void flush_() override;
    /// @brief Injected output/storage object; persistence belongs to its implementation.
    std::shared_ptr<log_output> output_;
    /// @brief Shared backend counters retained by the sink's output path.
    std::shared_ptr<logging_counts> counts_;
    /// @brief Immutable policy/configuration retained by this object.
    logging_options options_;
    /// @brief Bytes accepted since delegated rotation, assuming the supplied output starts empty.
    std::size_t bytes_{0};
};
} // namespace atlas::diagnostics::detail

namespace atlas::diagnostics
{
/// @brief Private implementation state; callers interact through the enclosing public API.
struct logger::impl
{
    /// @brief Validate logging options and create an explicitly owned sink and optional private
    /// backend.
    impl(logging_options options, std::shared_ptr<log_output> output);
    /// @brief Borrow the diagnostic category associated with this task or create its logging
    /// backend.
    std::shared_ptr<spdlog::logger> category(std::string_view name);
    /// @brief Post a non-droppable control flush and wait for backend acknowledgement outside
    /// admission locks.
    bool flush_backend() noexcept;
    /// @brief Explicit policy/configuration retained for this execution or subsystem.
    logging_options options;
    /// @brief Shared observation counters and flush acknowledgement state.
    std::shared_ptr<detail::logging_counts> counts{std::make_shared<detail::logging_counts>()};
    /// @brief Private structured sink serializing output callbacks.
    std::shared_ptr<detail::output_sink> sink;
    /// @brief Private async logging backend with one persistent thread.
    std::shared_ptr<spdlog::details::thread_pool> pool;
    /// @brief Cached category loggers; no global spdlog registry is used.
    std::map<std::string, std::shared_ptr<spdlog::logger>, std::less<>> categories;
    /// @brief Mutex protecting logger category creation and producer/flush admission state.
    mutable std::mutex admission;
    /// @brief Mutex serializing administrative flush/shutdown operations.
    std::mutex lifecycle;
    /// @brief Whether new root submissions/requests/messages are still admitted.
    bool accepting{true};
    /// @brief Whether producer attempts are counted as dropped while administrative output is
    /// draining.
    bool flushing{false};
    /// @brief Producer attempts rejected during a logger's administrative flush.
    std::uint64_t paused_dropped{0};
    /// @brief Async overflow count retained after the private backend has been released.
    std::uint64_t retired_dropped{0};
};
} // namespace atlas::diagnostics
