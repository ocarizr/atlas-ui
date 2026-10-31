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
enum class log_level : std::uint8_t
{
    trace,
    debug,
    info,
    warning,
    error,
    critical,
    off
};

struct logging_options
{
    log_level level{log_level::info};
    bool asynchronous{true};
    std::size_t queue_capacity{4096};
    std::size_t max_categories{64};
    std::size_t max_message_bytes{16384};
    std::size_t rotation_bytes{0};
    std::size_t retained_files{3};
    std::vector<std::pair<std::string, log_level>> category_levels;
};

// Storage owns paths, handles and archive naming. Output methods must not reenter logging.
struct log_record
{
    log_level level;
    std::string_view category, message, formatted;
    std::chrono::system_clock::time_point timestamp;
    std::uint64_t thread_id;
};

class log_output
{
  public:
    virtual ~log_output() = default;
    virtual void append(std::string_view text) = 0;
    // Borrowed views: copy fields when forwarding records to another thread/editor.
    virtual void record(const log_record& entry)
    {
        append(entry.formatted);
    }
    virtual void flush() = 0;
    virtual bool supports_rotation() const noexcept
    {
        return false;
    }
    virtual void rotate(std::size_t retained_files);
};

struct log_statistics
{
    std::uint64_t submitted{0};
    std::uint64_t filtered{0};
    std::uint64_t written{0};
    std::uint64_t dropped{0};
    std::uint64_t errors{0};
};

void validate(const logging_options& options);

class logger
{
  public:
    explicit logger(logging_options options = {}, std::shared_ptr<log_output> output = {});
    ~logger();
    logger(const logger&) = delete;
    logger& operator=(const logger&) = delete;
    // Configuration-only query; use it before expensive message construction.
    [[nodiscard]] bool enabled(log_level level, std::string_view category) const noexcept;
    // True means submitted to the backend; bounded async overflow can still discard it.
    bool write(log_level level, std::string_view category, std::string_view message) noexcept;
    // Coordinator operations: flush drains prior admitted messages; shutdown also joins.
    bool flush() noexcept;
    void shutdown() noexcept;
    [[nodiscard]] log_statistics statistics() const;

  private:
    struct impl;
    std::unique_ptr<impl> impl_;
};
} // namespace atlas::diagnostics
