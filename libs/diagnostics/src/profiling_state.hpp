/**
 * @file
 * @brief Private owned event storage and validated borrowed capture requests.
 */

#pragma once

#include <atlas/diagnostics/profiling.hpp>

#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace atlas::diagnostics::detail
{
/// @brief Internal Chrome trace event phase used by validated capture requests.
enum class event_phase : char
{
    /// @brief Complete execution interval represented by a Chrome trace duration event.
    duration = 'X',
    /// @brief Point-in-time observation with no implied execution duration.
    instant = 'i',
    /// @brief Cumulative numeric aggregation or a profiling counter event in its enclosing enum.
    counter = 'C'
};

/// @brief Borrowed validated event input used only while appending one profiling observation.
struct capture_request
{
    /// @brief Borrowed name/category labels copied only when capture admission succeeds.
    std::string_view name, category;
    /// @brief Borrowed start/end timestamps delimiting the requested steady-clock interval.
    profiler::clock::time_point start, end;
    /// @brief Borrowed event correlation metadata copied only when event admission succeeds.
    event_context context;
    /// @brief Original measured execution thread, not necessarily the finishing observer thread.
    std::thread::id thread;
    /// @brief Internal Chrome event discriminator controlling required numeric fields.
    event_phase phase;
    /// @brief Finite numeric payload used by a profiling counter event.
    double value;
};

/// @brief Owned event payload and stable numeric attribution retained by a bounded capture.
struct captured_event
{
    /// @brief Owned event name, category and result text, respectively.
    std::string name, category, result;
    /// @brief Chrome event phase or current synthetic-workload stage.
    char phase{'X'};
    /// @brief Relative timestamp and duration in nanoseconds from the capture's steady-clock
    /// origin.
    std::int64_t timestamp{0}, duration{0};
    /// @brief Thread/capture identity used for diagnostic attribution by this enclosing type.
    std::uint64_t thread{0}, id{0}, parent_id{0};
    /// @brief Optional numeric scheduling-class metadata copied into the event.
    std::uint32_t priority{0};
    /// @brief Current numeric observation or explicit scenario/workload value, according to its
    /// enclosing type.
    double value{0};
    /// @brief Time between submission and prerequisite eligibility.
    std::chrono::nanoseconds dependency_wait{}, queue_wait{}, join{}, total{};
};

/// @brief Validate complete UTF-8 sequences, rejecting truncation, overlong forms and invalid code
/// points.
bool valid_utf8(std::string_view text) noexcept;
/// @brief Escape a validated UTF-8 string for JSON without changing caller stream formatting.
void write_json_string(std::ostream& output, std::string_view value);
/// @brief Serialize one captured event with phase-specific fields and timing metadata.
void write_event(std::ostream& output, const captured_event& event, std::uint32_t process);
} // namespace atlas::diagnostics::detail

namespace atlas::diagnostics
{
/// @brief Private implementation state; callers interact through the enclosing public API.
struct profiler::impl
{
    /// @brief Copy a validated event into bounded owned capture storage under its mutex.
    bool append(const detail::capture_request& request);
    /// @brief Validate capture limits and reserve event capacity only when capture is enabled.
    explicit impl(profiling_options configuration);
    /// @brief Validate one borrowed profiling request and contain allocation/capture failures.
    bool record(std::string_view name, std::string_view category, clock::time_point start,
                clock::time_point end, event_context context, std::thread::id thread,
                detail::event_phase phase, double value) noexcept;
    /// @brief Explicit policy/configuration retained for this execution or subsystem.
    profiling_options options;
    /// @brief Steady-clock origin of this capture's exported relative timestamps.
    const clock::time_point epoch{clock::now()};
    /// @brief Synchronization for the enclosing shared registry/capture/execution state.
    mutable std::mutex mutex;
    /// @brief Bounded owned profiling event payloads.
    std::vector<detail::captured_event> events;
    /// @brief Map from observed OS threads to stable capture-local numeric tracks.
    std::map<std::thread::id, std::uint64_t> threads;
    /// @brief Retained profiling event, loss, error and string-byte accounting.
    profile_statistics counts;
};
} // namespace atlas::diagnostics
