/**
 * @file
 * @brief Explicit structured logging, filtering and bounded producer/backend lifecycle.
 */

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace atlas::diagnostics
{
/// @brief Structured logging severity ordered from trace through disabled output.
enum class log_level : std::uint8_t
{
    /// @brief Most verbose structured logging level.
    trace,
    /// @brief Detailed diagnostic logging below the default information threshold.
    debug,
    /// @brief Ordinary operational logging severity.
    info,
    /// @brief Operational warning severity above ordinary informational messages.
    warning,
    /// @brief Operational error severity below critical failures.
    error,
    /// @brief Highest enabled logging severity for severe failures.
    critical,
    /// @brief Disable output for the matching category or logger.
    off
};

/// @brief Immutable severity, category, queue and delegated-rotation configuration for one logger.
struct logging_options
{
    /// @brief Default minimum severity; category overrides may select a different threshold.
    log_level level{log_level::info};
    /// @brief Whether an explicitly constructed logger uses its private bounded backend thread.
    bool asynchronous{true};
    /// @brief Maximum pending asynchronous log messages before oldest-message overflow.
    std::size_t queue_capacity{4096};
    /// @brief Maximum user-created logging categories, excluding the internal flush control logger.
    std::size_t max_categories{64};
    /// @brief Largest accepted message payload in bytes.
    std::size_t max_message_bytes{16384};
    /// @brief Delegated rotation threshold; zero disables archive rotation.
    std::size_t rotation_bytes{0};
    /// @brief Requested archive retention supplied to a storage-owned rotating output.
    std::size_t retained_files{3};
    /// @brief Explicit per-category severity overrides with unique category names.
    std::vector<std::pair<std::string, log_level>> category_levels;
};

/// @brief Call-scoped structured message views; copy fields before asynchronous forwarding.
struct log_record
{
    /// @brief Severity of the structured record being delivered.
    log_level level;
    /// @brief Borrowed category, message and formatted text; copy each view before retaining
    /// asynchronously.
    std::string_view category, message, formatted;
    /// @brief Producer wall-clock timestamp supplied with this record.
    std::chrono::system_clock::time_point timestamp;
    /// @brief Original producer thread identity, independent of the output backend's thread.
    std::uint64_t thread_id;
};

/// @brief Injected output/storage boundary; implementations own persistence and must not reenter
/// their logger.
///
/// Do not reenter the same logger from output methods. Paths, archive names and persistence remain
/// behind the application storage boundary.
class log_output
{
  public:
    /// @brief Release log output resources and complete its scoped cleanup.
    virtual ~log_output() = default;
    /// @brief Accept formatted text from the serialized output path; persistence is owned by the
    /// implementation.
    ///
    /// @param text Borrowed formatted text valid only for this call.
    virtual void append(std::string_view text) = 0;

    /// Borrowed views: copy fields when forwarding records to another thread/editor.
    virtual void record(const log_record& entry)
    {
        append(entry.formatted);
    }

    /// @brief Flush implementation-owned output; the logger invokes this on its selected output
    /// path.
    virtual void flush() = 0;

    /// @brief Report whether the injected output implements storage-owned archive rotation.
    virtual bool supports_rotation() const noexcept
    {
        return false;
    }

    /// @brief Delegate rotation/retention to the output; the base implementation rejects
    /// unsupported rotation.
    ///
    /// @param retained_files Requested archive retention delegated to storage.
    virtual void rotate(std::size_t retained_files);
};

/// @brief Observational logging admission, filtering, output, loss and failure counts.
struct log_statistics
{
    /// @brief Messages admitted to the logger backend, including messages later lost to async
    /// overflow.
    std::uint64_t submitted{0};
    /// @brief Messages rejected by immutable severity/category configuration.
    std::uint64_t filtered{0};
    /// @brief Records successfully accepted by the injected output.
    std::uint64_t written{0};
    /// @brief Messages lost to bounded async overflow or rejected during administrative flush.
    std::uint64_t dropped{0};
    /// @brief Contained validation, formatting and output failures.
    std::uint64_t errors{0};
};

/// @brief Reject invalid configuration, handles or descriptors before mutating execution state.
///
/// @param options Explicit validated configuration/policy for this instance.
void validate(const logging_options& options);

/// @brief Explicitly owned structured logger with a private optional async backend and bounded
/// admission.
///
/// Async admission can drop old messages or reject producers during flush. Inspect statistics;
/// successful write admission is not persistence acknowledgement. Stop callers before destruction.
class logger
{
  public:
    /// @brief Create an owned logger with explicit limits and an optional injected output.
    ///
    /// @param options Explicit validated configuration/policy for this instance.
    /// @param output Caller-owned output/stream; storage and transport policy belong to its
    /// implementation.
    explicit logger(logging_options options = {}, std::shared_ptr<log_output> output = {});
    /// @brief Drain accepted work and release persistent resources; callers must obey the enclosing
    /// coordinator/lifetime contract.
    ~logger();
    /// @brief Create an owned logger with explicit limits and an optional injected output.
    logger(const logger&) = delete;
    /// @brief Disable copy construction/assignment to preserve unique lifecycle ownership.
    logger& operator=(const logger&) = delete;
    /// Configuration-only query; use it before expensive message construction.
    [[nodiscard]] bool enabled(log_level level, std::string_view category) const noexcept;
    /// True means submitted to the backend; bounded async overflow can still discard it.
    bool write(log_level level, std::string_view category, std::string_view message) noexcept;
    /// Coordinator operations: flush drains prior admitted messages; shutdown also joins.
    bool flush() noexcept;
    /// @brief Stop accepting new roots, drain accepted work and join owned workers; never call from
    /// a job.
    void shutdown() noexcept;
    /// @brief Return an observational snapshot of this instance's counters without resetting them.
    [[nodiscard]] log_statistics statistics() const;

  private:
    /// @brief Private implementation state; callers interact through the enclosing public API.
    struct impl;
    /// @brief Owned private implementation storage; destruction follows the enclosing lifecycle
    /// contract.
    std::unique_ptr<impl> impl_;
};
} // namespace atlas::diagnostics
