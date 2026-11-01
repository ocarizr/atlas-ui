/**
 * @file
 * @brief Bounded steady-clock capture and owned RAII duration labels.
 */

#include "profiling_state.hpp"

#include <cmath>
#include <stdexcept>

namespace atlas::diagnostics
{
/// @brief Reject invalid configuration, handles or descriptors before mutating execution state.
void validate(const profiling_options& options)
{
    if (!options.max_events || !options.max_string_bytes || !options.process_id)
    {
        throw std::invalid_argument{"Profiling capacities and process ID must be positive"};
    }
}

/// @brief Validate capture limits and reserve event capacity only when capture is enabled.
profiler::impl::impl(profiling_options configuration) : options{configuration}
{
    validate(options);
    if (options.enabled)
    {
        events.reserve(options.max_events);
    }
}

/// @brief Create an empty bounded steady-clock event capture with explicit configuration.
profiler::profiler(profiling_options options) : impl_{std::make_unique<impl>(options)}
{
}

/// @brief Release profiler resources and complete its scoped cleanup.
profiler::~profiler() = default;

/// @brief Validate one borrowed profiling request and contain allocation/capture failures.
bool profiler::impl::record(std::string_view name, std::string_view category,
                            clock::time_point start, clock::time_point end, event_context context,
                            std::thread::id thread, detail::event_phase phase,
                            double value) noexcept
{
    const std::lock_guard lock{mutex};
    if (!options.enabled)
    {
        return false;
    }

    if (name.empty() || start < epoch || end < start || !std::isfinite(value) ||
        !detail::valid_utf8(name) || !detail::valid_utf8(category) ||
        !detail::valid_utf8(context.result))
    {
        ++counts.errors;
        return false;
    }

    const auto remaining = options.max_string_bytes - counts.string_bytes;
    if (events.size() == options.max_events || name.size() > remaining ||
        category.size() > remaining - name.size() ||
        context.result.size() > remaining - name.size() - category.size())
    {
        ++counts.dropped;
        return false;
    }

    try
    {
        return append({name, category, start, end, context, thread, phase, value});
    }
    catch (...)
    {
        ++counts.errors;
        return false;
    }
}

/// @brief Copy a validated event into bounded owned capture storage under its mutex.
bool profiler::impl::append(const detail::capture_request& request)
{
    const auto& [name, category, start, end, context, thread, phase, value] = request;
    auto found = threads.find(thread);
    if (found == threads.end())
    {
        if (threads.size() == options.max_events)
        {
            ++counts.dropped;
            return false;
        }

        found = threads.emplace(thread, threads.size() + 1).first;
    }

    detail::captured_event event;
    event.name = name;
    event.category = category;
    event.result = context.result;
    event.phase = static_cast<char>(phase);
    event.timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(start - epoch).count();
    event.duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    event.thread = found->second;
    event.id = context.id;
    event.parent_id = context.parent_id;
    event.priority = context.priority;
    event.value = value;
    event.dependency_wait = context.dependency_wait;
    event.queue_wait = context.queue_wait;
    event.join = context.join;
    event.total = context.total;
    events.push_back(std::move(event));
    counts.string_bytes += name.size() + category.size() + context.result.size();
    ++counts.recorded;
    return true;
}

/// @brief Record a validated steady-clock execution interval on the original execution thread.
bool profiler::duration(std::string_view name, std::string_view category, clock::time_point start,
                        clock::time_point end, event_context context,
                        std::thread::id thread) noexcept
{
    return impl_->record(name, category, start, end, context, thread, detail::event_phase::duration,
                         0);
}

/// @brief Record a point-in-time observation without implying an execution interval.
bool profiler::instant(std::string_view name, std::string_view category,
                       event_context context) noexcept
{
    const auto now = clock::now();
    return impl_->record(name, category, now, now, context, std::this_thread::get_id(),
                         detail::event_phase::instant, 0);
}

/// @brief Record a numeric event in the profiling timeline; this is not metric aggregation.
bool profiler::counter(std::string_view name, std::string_view category, double value) noexcept
{
    const auto now = clock::now();
    return impl_->record(name, category, now, now, {}, std::this_thread::get_id(),
                         detail::event_phase::counter, value);
}

/// @brief Return an observational snapshot of this instance's counters without resetting them.
profile_statistics profiler::statistics() const
{
    const std::lock_guard lock{impl_->mutex};
    return impl_->counts;
}

/// @brief Discard collected profiling events and reset capture accounting for a new capture window.
void profiler::clear()
{
    const std::lock_guard lock{impl_->mutex};
    impl_->events.clear();
    impl_->threads.clear();
    impl_->counts = {};
}

/// @brief Copy scope labels and capture start time while borrowing a profiler that must outlive the
/// scope.
profile_scope::profile_scope(profiler& capture, std::string_view name,
                             std::string_view category) noexcept
    : capture_{capture}, start_{profiler::clock::now()}
{
    try
    {
        name_ = name;
        category_ = category;
        ready_ = true;
    }
    catch (...)
    {
        (void)capture_.instant({}, {});
    }
}

/// @brief Record the elapsed interval only if owned scope-label initialization succeeded.
profile_scope::~profile_scope()
{
    if (ready_)
    {
        (void)capture_.duration(name_, category_, start_, profiler::clock::now());
    }
}
} // namespace atlas::diagnostics
