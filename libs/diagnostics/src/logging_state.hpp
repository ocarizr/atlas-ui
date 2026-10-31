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
inline thread_local bool inside_output = false;

struct output_scope
{
    output_scope()
    {
        inside_output = true;
    }
    ~output_scope()
    {
        inside_output = false;
    }
    output_scope(const output_scope&) = delete;
    output_scope& operator=(const output_scope&) = delete;
};

struct logging_counts
{
    std::atomic<std::uint64_t> submitted{0}, filtered{0}, written{0}, errors{0};
    std::mutex mutex;
    std::condition_variable flushed;
    std::uint64_t flush_count{0};
};

class output_sink final : public spdlog::sinks::base_sink<std::mutex>
{
  public:
    output_sink(std::shared_ptr<log_output> output, std::shared_ptr<logging_counts> counts,
                logging_options options);

  private:
    void sink_it_(const spdlog::details::log_msg& message) override;
    void flush_() override;
    std::shared_ptr<log_output> output_;
    std::shared_ptr<logging_counts> counts_;
    logging_options options_;
    std::size_t bytes_{0};
};
} // namespace atlas::diagnostics::detail

namespace atlas::diagnostics
{
struct logger::impl
{
    impl(logging_options options, std::shared_ptr<log_output> output);
    std::shared_ptr<spdlog::logger> category(std::string_view name);
    bool flush_backend() noexcept;
    logging_options options;
    std::shared_ptr<detail::logging_counts> counts{std::make_shared<detail::logging_counts>()};
    std::shared_ptr<detail::output_sink> sink;
    std::shared_ptr<spdlog::details::thread_pool> pool;
    std::map<std::string, std::shared_ptr<spdlog::logger>, std::less<>> categories;
    mutable std::mutex admission;
    std::mutex lifecycle;
    bool accepting{true};
    bool flushing{false};
    std::uint64_t paused_dropped{0};
    std::uint64_t retired_dropped{0};
};
} // namespace atlas::diagnostics
