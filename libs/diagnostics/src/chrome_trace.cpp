/**
 * @file
 * @brief UTF-8 validation and Chrome Trace Event JSON serialization to caller-owned streams.
 */

#include "profiling_state.hpp"

#include <charconv>
#include <locale>
#include <ostream>
#include <sstream>
#include <stdexcept>

namespace atlas::diagnostics::detail
{
/// @brief Validate complete UTF-8 sequences, rejecting truncation, overlong forms and invalid code
/// points.
bool valid_utf8(std::string_view text) noexcept
{
    std::uint32_t code = 0, minimum = 0;
    int remaining = 0;
    for (const unsigned char byte : text)
    {
        if (remaining)
        {
            if ((byte & 0xc0U) != 0x80U)
            {
                return false;
            }

            code = (code << 6U) | (byte & 0x3fU);
            if (--remaining == 0 &&
                (code < minimum || code > 0x10ffffU || (code >= 0xd800U && code <= 0xdfffU)))
            {
                return false;
            }
        }
        else if (byte < 0x80U)
        {
            continue;
        }
        else if (byte >= 0xc2U && byte <= 0xdfU)
        {
            remaining = 1;
            code = byte & 0x1fU;
            minimum = 0x80;
        }
        else if (byte >= 0xe0U && byte <= 0xefU)
        {
            remaining = 2;
            code = byte & 0x0fU;
            minimum = 0x800;
        }
        else if (byte >= 0xf0U && byte <= 0xf4U)
        {
            remaining = 3;
            code = byte & 0x07U;
            minimum = 0x10000;
        }
        else
        {
            return false;
        }
    }

    return remaining == 0;
}

/// @brief Escape a validated UTF-8 string for JSON without changing caller stream formatting.
void write_json_string(std::ostream& output, std::string_view value)
{
    static constexpr char digits[] = "0123456789abcdef";
    output.put('"');
    for (const unsigned char byte : value)
    {
        switch (byte)
        {
            case '"':
                output << "\\\"";
                break;
            case '\\':
                output << "\\\\";
                break;
            case '\n':
                output << "\\n";
                break;
            case '\r':
                output << "\\r";
                break;
            case '\t':
                output << "\\t";
                break;
            default:
                if (byte < 32U)
                {
                    output << "\\u00" << digits[byte >> 4U] << digits[byte & 15U];
                }
                else
                {
                    output.put(static_cast<char>(byte));
                }
        }
    }

    output.put('"');
}

namespace
{
/// @brief Serialize nanosecond timestamps as microseconds without losing fractional precision.
void write_microseconds(std::ostream& output, std::int64_t nanoseconds)
{
    const auto fraction = nanoseconds % 1000;
    output << nanoseconds / 1000 << '.' << fraction / 100 << (fraction / 10) % 10 << fraction % 10;
}
} // namespace

/// @brief Serialize one captured event with phase-specific fields and timing metadata.
void write_event(std::ostream& output, const captured_event& event, std::uint32_t process)
{
    output << "{\"name\":";
    write_json_string(output, event.name);
    output << ",\"cat\":";
    write_json_string(output, event.category);
    output << ",\"ph\":\"" << event.phase << "\",\"pid\":" << process << ",\"tid\":" << event.thread
           << ",\"ts\":";
    write_microseconds(output, event.timestamp);
    if (event.phase == 'X')
    {
        output << ",\"dur\":";
        write_microseconds(output, event.duration);
    }

    if (event.phase == 'i')
    {
        output << ",\"s\":\"t\"";
    }

    output << ",\"args\":{";
    if (event.phase == 'C')
    {
        char buffer[64];
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer), event.value);
        if (result.ec != std::errc{})
        {
            throw std::runtime_error{"Counter serialization failed"};
        }

        output << "\"value\":";
        output.write(buffer, result.ptr - buffer);
    }
    else
    {
        output << "\"id\":" << event.id << ",\"parent_id\":" << event.parent_id
               << ",\"priority\":" << event.priority << ",\"result\":";
        write_json_string(output, event.result);
        output << ",\"dependency_wait_ns\":" << event.dependency_wait.count()
               << ",\"queue_wait_ns\":" << event.queue_wait.count()
               << ",\"join_ns\":" << event.join.count() << ",\"total_ns\":" << event.total.count();
    }

    output << "}}";
}
} // namespace atlas::diagnostics::detail

namespace atlas::diagnostics
{
/// @brief Copy bounded capture state and serialize Chrome Trace Event JSON to a caller-owned
/// stream.
void profiler::write_chrome_trace(std::ostream& output) const
{
    std::vector<detail::captured_event> events;
    profile_statistics counts;
    {
        const std::lock_guard lock{impl_->mutex};
        events = impl_->events;
        counts = impl_->counts;
    }
    // A local classic-locale stream avoids mutating caller formatting or emitting grouped numbers.
    std::ostringstream document;
    document.imbue(std::locale::classic());
    document << "{\"traceEvents\":[";
    bool first = true;
    for (const auto& event : events)
    {
        if (!first)
        {
            document << ',';
        }

        first = false;
        detail::write_event(document, event, impl_->options.process_id);
    }

    document << "],\"displayTimeUnit\":\"ms\",\"metadata\":{\"dropped\":" << counts.dropped
             << ",\"errors\":" << counts.errors << "}}";
    output << document.str();
    if (!output)
    {
        throw std::runtime_error{"Trace output failed"};
    }
}
} // namespace atlas::diagnostics
