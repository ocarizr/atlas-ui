/**
 * @file
 * @brief Serialized injected outputs, delegated archive rotation and contained output failures.
 */

#include "logging_state.hpp"

#include <iostream>
#include <stdexcept>

namespace atlas::diagnostics
{
/// @brief Delegate rotation/retention to the output; the base implementation rejects unsupported
/// rotation.
void log_output::rotate(std::size_t)
{
    throw std::logic_error{"Output cannot rotate"};
}

namespace detail
{
namespace
{
/// @brief Default stream output for an explicitly constructed logger; no file paths are opened.
class console_output final : public log_output
{
    /// @brief Write a formatted record to the console stream and report stream failure.
    void append(std::string_view text) override
    {
        std::clog.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!std::clog)
        {
            throw std::runtime_error{"Console write failed"};
        }
    }

    /// @brief Flush the console stream and report output failure.
    void flush() override
    {
        std::clog.flush();
        if (!std::clog)
        {
            throw std::runtime_error{"Console flush failed"};
        }
    }
};
} // namespace

/// @brief Bind serialized output and validate delegated rotation support before accepting messages.
output_sink::output_sink(std::shared_ptr<log_output> output, std::shared_ptr<logging_counts> counts,
                         logging_options options)
    : output_{output ? std::move(output) : std::make_shared<console_output>()},
      counts_{std::move(counts)}, options_{std::move(options)}
{
    if (options_.rotation_bytes && !output_->supports_rotation())
    {
        throw std::invalid_argument{"Rotation needs a storage-backed rotating output"};
    }

    set_pattern("[%Y-%m-%d %H:%M:%S.%e] [thread %t] [%l] [%n] %v");
}

/// @brief Format and deliver a record under the sink lock, containing all output exceptions.
void output_sink::sink_it_(const spdlog::details::log_msg& message)
{
    output_scope guard;
    try
    {
        spdlog::memory_buf_t formatted;
        formatter_->format(message, formatted);
        const std::string_view text{formatted.data(), formatted.size()};
        if (options_.rotation_bytes && bytes_ &&
            (text.size() > options_.rotation_bytes ||
             bytes_ > options_.rotation_bytes - text.size()))
        {
            output_->flush();
            output_->rotate(options_.retained_files);
            bytes_ = 0;
        }

        output_->record({static_cast<log_level>(message.level),
                         {message.logger_name.data(), message.logger_name.size()},
                         {message.payload.data(), message.payload.size()},
                         text,
                         message.time,
                         message.thread_id});
        if (options_.rotation_bytes)
        {
            bytes_ += text.size();
        }

        ++counts_->written;
    }
    catch (...)
    {
        ++counts_->errors;
    }
}

/// @brief Flush the injected output and always acknowledge completion, including output failures.
void output_sink::flush_()
{
    output_scope guard;
    try
    {
        output_->flush();
    }
    catch (...)
    {
        ++counts_->errors;
    }

    {
        const std::lock_guard lock{counts_->mutex};
        ++counts_->flush_count;
    }
    counts_->flushed.notify_all();
}
} // namespace detail
} // namespace atlas::diagnostics
