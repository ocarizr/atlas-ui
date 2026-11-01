/**
 * @file
 * @brief Optional terminal observation without adding diagnostic vendors to the scheduler.
 */

#pragma once
#include <atlas/jobs/job_graph.hpp>
#include <atlas/jobs/job_policy.hpp>

#include <chrono>
#include <cstdint>
#include <exception>
#include <string_view>
#include <thread>

namespace atlas::jobs
{
/// @brief Borrowed immutable terminal metadata delivered after source completion publication.
struct task_event
{
    /// @brief Runtime-local task identity used for diagnostic correlation.
    std::uint64_t id;
    /// @brief Runtime-local parent identity; zero identifies an independent root.
    std::uint64_t parent_id;
    /// @brief Borrowed task name valid only during the observer call.
    std::string_view name;
    /// @brief Borrowed diagnostic category valid only during the observer call.
    std::string_view category;
    /// @brief Immutable terminal source result, already published before observation begins.
    job_status status;
    /// @brief Effective weighted dispatch class; it does not imply callback preemption.
    job_priority priority;
    /// @brief Original callback execution thread, even when another worker finishes its join.
    std::thread::id execution_thread;
    /// @brief False for skipped, undispatched cancelled or abandoned notification bodies.
    bool body_executed;
    /// @brief Steady-clock admission timestamp used for latency metadata.
    std::chrono::steady_clock::time_point submitted;
    /// @brief Time the task became eligible for dispatch.
    std::chrono::steady_clock::time_point queued;
    /// @brief Time its body began executing, if dispatched.
    std::chrono::steady_clock::time_point started;
    /// @brief Time its body returned, before joining any remaining children.
    std::chrono::steady_clock::time_point body_finished;
    /// @brief Time the terminal task result was published.
    std::chrono::steady_clock::time_point completed;
    /// @brief Original immutable failure carried by terminal task observation.
    std::exception_ptr exception;
};

/// Borrowed metadata is valid for this call only. Implementations must not throw,
/// block on external work, wait on jobs, or retain borrowed views.
class job_observer
{
  public:
    /// @brief Release job observer resources and complete its scoped cleanup.
    virtual ~job_observer() = default;
    /// @brief Observe terminal direct-task metadata; never wait on the scheduler or perform
    /// external I/O here.
    ///
    /// @param event Terminal outcome filter; notification failures never replace source outcomes.
    virtual void completed(const task_event& event) noexcept = 0;
};
} // namespace atlas::jobs
