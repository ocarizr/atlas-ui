/**
 * @file
 * @brief Shared worker-pool lifecycle, coordinator waits and aggregate observation.
 */

#pragma once

#include <atlas/jobs/job_dispatcher.hpp>
#include <atlas/jobs/job_graph.hpp>
#include <atlas/jobs/job_observer.hpp>

#include <array>
#include <cstddef>
#include <functional>
#include <future>
#include <limits>
#include <memory>

namespace atlas::jobs
{
/// @brief Composition-root worker, scratch, access-checking and optional observation configuration.
struct job_system_options
{
    /// @brief Configured persistent worker count; must be positive.
    std::size_t worker_count{1};
    /// @brief Declared-access checker configuration/state; disabled checking does not remove basic
    /// contracts.
    job_sanitizer_options sanitizer;
    /// @brief Task-owned temporary resource or its terminal allocation statistics.
    scratch_options scratch;
    /// @brief Maximum admitted direct-task records; zero disables bounded tracing.
    std::size_t trace_capacity{0};
    /// @brief Optional retained terminal observer; source publication precedes delivery, but
    /// idle/shutdown drains it.
    std::shared_ptr<job_observer> observer;
};

/// @brief Application-owned persistent CPU worker pool; inject dispatchers instead of creating
/// consumer pools.
///
/// External producers must stop before destruction. Jobs, notifications and terminal observers must
/// not wait or shut down the pool. Shutdown drains accepted contexts and allows their attached
/// children.
class job_system;

/// @brief Legacy independent-job completion with weak runtime identity for foreign-handle
/// rejection.
class job_handle
{
  public:
    /// @brief Create an empty legacy handle or retain its shared completion and weak runtime
    /// identity.
    job_handle() = default;

  private:
    friend class job_system;
    /// @brief Create an empty legacy handle or retain its shared completion and weak runtime
    /// identity.
    ///
    /// @param completion Shared terminal completion state.
    /// @param owner Weak runtime identity used to validate ownership.
    job_handle(std::shared_future<void> completion, std::weak_ptr<const void> owner);

    /// @brief Retained legacy completion state, independently observable by coordinator waits.
    std::shared_future<void> completion_;
    /// @brief Identity used to reject handles from a different runtime or graph builder.
    std::weak_ptr<const void> owner_;
};

/// @brief Observational pool counts; queued/running/waiting and terminal totals describe different
/// states.
struct job_statistics
{
    /// @brief Number of persistent workers owned by this pool.
    std::size_t workers;
    /// @brief Work ready for dispatch, excluding currently executing callbacks.
    std::size_t queued;
    /// @brief Callbacks currently executing on CPU workers.
    std::size_t running;
    /// @brief Lifetime count of admitted callbacks/tasks.
    std::size_t submitted;
    /// @brief Lifetime terminal count, including failed, skipped and cancelled tasks.
    std::size_t completed; /// Terminal jobs, including failed and skipped jobs.
    /// @brief Accepted tasks awaiting prerequisites, descendants or cleanup.
    std::size_t waiting; /// Jobs awaiting prerequisites, children, or cleanup.
    /// @brief Lifetime real-failure outcomes, including parents inheriting descendant failures.
    std::size_t failed; /// Failed outcomes, including parents with failed children.
    /// @brief Bodies suppressed by unsuccessful prerequisites.
    std::size_t skipped; /// Callbacks suppressed by failed/cancelled prerequisites.
    /// @brief Origin-affine callback dispatch totals indexed by priority class.
    std::array<std::size_t, 3> callbacks_dispatched_by_priority;
    /// @brief Notifications executed by coordinator threads rather than workers.
    std::size_t caller_dispatched; /// Notification tasks executed by caller threads.
    /// @brief Lifetime cooperative cancellation outcomes, including joined cancelled children.
    std::size_t cancelled; /// Cancelled outcomes, including parents joining cancelled children.
    /// @brief Ready task counts indexed by high/normal/background priority.
    std::array<std::size_t, 3> queued_by_priority;
    /// @brief Observed dispatch totals indexed by high/normal/background priority.
    std::array<std::size_t, 3> dispatched_by_priority;
};

/// Jobs must not call wait, wait_idle, shutdown, or destroy a scheduler.
class job_system
{
  public:
    /// @brief Create the configured persistent pool; invalid worker counts/options are rejected
    /// before use.
    ///
    /// @param worker_count Positive number of persistent CPU workers.
    /// @param sanitizer Optional declared-access checking configuration.
    explicit job_system(std::size_t worker_count, job_sanitizer_options sanitizer = {});
    /// @brief Create the configured persistent pool; invalid worker counts/options are rejected
    /// before use.
    ///
    /// @param options Explicit validated configuration/policy for this instance.
    explicit job_system(job_system_options options);
    /// @brief Drain accepted work and release persistent resources; callers must obey the enclosing
    /// coordinator/lifetime contract.
    ~job_system();
    /// @brief Create the configured persistent pool; invalid worker counts/options are rejected
    /// before use.
    job_system(const job_system&) = delete;
    /// @brief Disable copy construction/assignment to preserve unique lifecycle ownership.
    job_system& operator=(const job_system&) = delete;
    /// @brief Create the configured persistent pool; invalid worker counts/options are rejected
    /// before use.
    job_system(job_system&&) = delete;
    /// @brief Disable move construction/assignment to preserve unique lifecycle ownership.
    job_system& operator=(job_system&&) = delete;

    /// @brief Admit a legacy independent callback and retain its completion result for coordinator
    /// observation.
    ///
    /// @param job Handle/ID belonging to this runtime or graph builder.
    job_handle submit(std::function<void()> job);
    /// @brief Return a non-owning client of the existing pool without creating worker threads.
    [[nodiscard]] job_dispatcher dispatcher() const;
    /// Validates and snapshots the entire graph before publishing any work.
    graph_handle submit_graph(const job_graph& graph);
    /// @brief Wait only from a coordinator, publish task effects and rethrow the recorded failure.
    ///
    /// @param job Handle/ID belonging to this runtime or graph builder.
    void wait(const job_handle& job) const;
    /// @brief Wait only from a coordinator, publish task effects and rethrow the recorded failure.
    ///
    /// @param graph Coordinator-built topology snapshotted before work publication.
    void wait(const graph_handle& graph) const;
    /// @brief Wait for accepted work and observer delivery to drain; concurrent producers may
    /// submit afterward.
    void wait_idle() const;
    /// @brief Deliver up to the requested number of this coordinator's queued notifications.
    ///
    /// @param limit Maximum caller notifications to execute in this pump operation.
    std::size_t dispatch_callbacks(std::size_t limit = std::numeric_limits<std::size_t>::max());
    /// @brief Stop accepting new roots, drain accepted work and join owned workers; never call from
    /// a job.
    void shutdown();
    /// @brief Return an observational snapshot of this instance's counters without resetting them.
    [[nodiscard]] job_statistics statistics() const;
    /// @brief Copy bounded declared-access diagnostics, including dropped-report counts.
    [[nodiscard]] job_sanitizer_report sanitizer_report() const;
    /// @brief Copy the bounded incremental-task trace; pending or dropped entries indicate an
    /// incomplete capture.
    [[nodiscard]] job_trace_report trace_report() const;

  private:
    /// @brief Owned private implementation storage; destruction follows the enclosing lifecycle
    /// contract.
    std::shared_ptr<detail::scheduler> impl_;
};
} // namespace atlas::jobs
