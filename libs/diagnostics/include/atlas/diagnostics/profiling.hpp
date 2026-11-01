/**
 * @file
 * @brief Bounded steady-clock capture and owned RAII duration labels.
 */

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

namespace atlas::diagnostics
{
/// @brief Capture enablement, event/string capacity and exported process identity.
struct profiling_options
{
    /// @brief Whether explicitly invoked profiling operations retain events.
    bool enabled{true};
    /// @brief Maximum retained profiling events; new events beyond it are counted as dropped.
    std::size_t max_events{65536};
    /// @brief Maximum retained profiling label/result bytes across admitted events.
    std::size_t max_string_bytes{std::size_t{8} * 1024 * 1024};
    /// @brief Explicit process identity written into Chrome trace events.
    std::uint32_t process_id{1};
};

/// @brief Recorded/dropped/error counts and retained event payload bytes for bounded capture.
struct profile_statistics
{
    /// @brief Events successfully retained by the bounded profiling capture.
    std::size_t recorded{0};
    /// @brief Events rejected by event-count or retained-string capacity.
    std::size_t dropped{0};
    /// @brief Invalid event observations or contained capture failures.
    std::size_t errors{0};
    /// @brief Retained profiling label/result payload bytes.
    std::size_t string_bytes{0};
};

/// @brief Optional task correlation and timing metadata copied into recorded events.
struct event_context
{
    /// @brief Optional runtime-local task identity copied into the event.
    std::uint64_t id{0};
    /// @brief Optional parent identity; zero denotes an independent or uncorrelated event.
    std::uint64_t parent_id{0};
    /// @brief Borrowed outcome text copied during event capture.
    std::string_view result;
    /// @brief Numeric scheduling class exported as event metadata.
    std::uint32_t priority{0};
    /// @brief Dependency, queue, join and total nanosecond metadata, respectively.
    std::chrono::nanoseconds dependency_wait{}, queue_wait{}, join{}, total{};
};

/// @brief Reject invalid configuration, handles or descriptors before mutating execution state.
///
/// @param options Explicit validated configuration/policy for this instance.
void validate(const profiling_options& options);

/// @brief Explicit bounded steady-clock capture whose owning snapshots serialize to Chrome Trace
/// Event JSON.
///
/// Recording is bounded and changes timings. Inspect pending/loss/error metadata where applicable;
/// a capture is not proof of deterministic scheduling or race freedom.
class profiler
{
  public:
    /// @brief Steady clock used to measure elapsed profiling intervals.
    using clock = std::chrono::steady_clock;
    /// @brief Create an empty bounded steady-clock event capture with explicit configuration.
    ///
    /// @param options Explicit validated configuration/policy for this instance.
    explicit profiler(profiling_options options = {});
    /// @brief Release profiler resources and complete its scoped cleanup.
    ~profiler();
    /// @brief Create an empty bounded steady-clock event capture with explicit configuration.
    profiler(const profiler&) = delete;
    /// @brief Disable copy construction/assignment to preserve unique lifecycle ownership.
    profiler& operator=(const profiler&) = delete;
    /// @brief Record a validated steady-clock execution interval on the original execution thread.
    ///
    /// @param name Case-sensitive diagnostic or metric name; metric names must be nonempty bounded
    /// UTF-8.
    /// @param category Diagnostic grouping or severity override key.
    /// @param start Steady-clock start at or after this profiler's creation.
    /// @param end Steady-clock end not preceding start.
    /// @param context Optional borrowed correlation metadata copied during capture.
    /// @param thread Original execution thread to attribute the measured body interval.
    bool duration(std::string_view name, std::string_view category, clock::time_point start,
                  clock::time_point end, event_context context = {},
                  std::thread::id thread = std::this_thread::get_id()) noexcept;
    /// @brief Record a point-in-time observation without implying an execution interval.
    ///
    /// @param name Case-sensitive diagnostic or metric name; metric names must be nonempty bounded
    /// UTF-8.
    /// @param category Diagnostic grouping or severity override key.
    /// @param context Optional borrowed correlation metadata copied during capture.
    bool instant(std::string_view name, std::string_view category,
                 event_context context = {}) noexcept;
    /// @brief Record a numeric event in the profiling timeline; this is not metric aggregation.
    ///
    /// @param name Case-sensitive diagnostic or metric name; metric names must be nonempty bounded
    /// UTF-8.
    /// @param category Diagnostic grouping or severity override key.
    /// @param value Finite numeric observation; gauges accept negative values.
    bool counter(std::string_view name, std::string_view category, double value) noexcept;
    /// @brief Return an observational snapshot of this instance's counters without resetting them.
    [[nodiscard]] profile_statistics statistics() const;
    /// @brief Discard collected profiling events and reset capture accounting for a new capture
    /// window.
    void clear();
    /// Takes an owning snapshot before writing. Storage and stream lifetime belong to the caller.
    void write_chrome_trace(std::ostream& output) const;

  private:
    /// @brief Private implementation state; callers interact through the enclosing public API.
    struct impl;
    /// @brief Owned private implementation storage; destruction follows the enclosing lifecycle
    /// contract.
    std::unique_ptr<impl> impl_;
};

/// @brief RAII duration capture owning its labels and borrowing a profiler that must outlive it.
class profile_scope
{
  public:
    /// Labels are owned; the profiler must survive this scope.
    profile_scope(profiler& capture, std::string_view name, std::string_view category) noexcept;
    /// @brief Record the elapsed interval only if owned scope-label initialization succeeded.
    ~profile_scope();
    /// @brief Copy scope labels and capture start time while borrowing a profiler that must outlive
    /// the scope.
    profile_scope(const profile_scope&) = delete;
    /// @brief Disable copy construction/assignment to preserve unique lifecycle ownership.
    profile_scope& operator=(const profile_scope&) = delete;

  private:
    /// @brief Borrowed profiler; the enclosing duration scope must be destroyed first.
    profiler& capture_;
    /// @brief Owned diagnostic scope label retained across temporary input strings.
    std::string name_, category_;
    /// @brief Scope's steady-clock start captured before its measured body.
    profiler::clock::time_point start_;
    /// @brief Whether scope label ownership initialized successfully and a duration may be
    /// recorded.
    bool ready_{false};
};
} // namespace atlas::diagnostics
