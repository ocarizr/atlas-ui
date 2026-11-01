#pragma once

#include <atlas/diagnostics/profiling.hpp>

#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace atlas::diagnostics::detail
{
enum class event_phase : char
{
    duration = 'X',
    instant = 'i',
    counter = 'C'
};
struct capture_request
{
    std::string_view name, category;
    profiler::clock::time_point start, end;
    event_context context;
    std::thread::id thread;
    event_phase phase;
    double value;
};
struct captured_event
{
    std::string name, category, result;
    char phase{'X'};
    std::int64_t timestamp{0}, duration{0};
    std::uint64_t thread{0}, id{0}, parent_id{0};
    std::uint32_t priority{0};
    double value{0};
    std::chrono::nanoseconds dependency_wait{}, queue_wait{}, join{}, total{};
};
bool valid_utf8(std::string_view text) noexcept;
void write_json_string(std::ostream& output, std::string_view value);
void write_event(std::ostream& output, const captured_event& event, std::uint32_t process);
} // namespace atlas::diagnostics::detail

namespace atlas::diagnostics
{
struct profiler::impl
{
    bool append(const detail::capture_request& request);
    explicit impl(profiling_options configuration);
    bool record(std::string_view name, std::string_view category, clock::time_point start,
                clock::time_point end, event_context context, std::thread::id thread,
                detail::event_phase phase, double value) noexcept;
    profiling_options options;
    const clock::time_point epoch{clock::now()};
    mutable std::mutex mutex;
    std::vector<detail::captured_event> events;
    std::map<std::thread::id, std::uint64_t> threads;
    profile_statistics counts;
};
} // namespace atlas::diagnostics
