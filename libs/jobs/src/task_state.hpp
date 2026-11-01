/**
 * @file
 * @brief Direct-task ownership, prerequisite links and scoped join metadata.
 */

#pragma once

#include "graph_state.hpp"
#include "scratch_resource.hpp"
#include <atlas/jobs/job_dispatcher.hpp>

#include <atomic>
#include <chrono>
#include <limits>
#include <thread>

namespace atlas::jobs::detail
{
/// @brief Origin-thread notification queue with explicit owner-exit and affine delivery state.
struct callback_mailbox;

/// @brief Unique retained owner token with the OS thread ID used only for diagnostic attribution.
struct thread_identity
{
    /// @brief OS thread ID captured when the unique owner token is created; ID equality alone does
    /// not establish ownership.
    std::thread::id value{std::this_thread::get_id()};
};

/// @brief Retain a unique owner token so recycled OS thread IDs cannot claim old notification
/// queues.
std::shared_ptr<const thread_identity> current_thread_identity();

/// @brief Steady clock used by direct-task submission/execution timing.
using task_clock = std::chrono::steady_clock;

/// @brief Retained failure ownership that can be destroyed outside scheduler critical sections.
struct retired_failure
{
    /// @brief Retained original task carrying a propagated user failure.
    std::shared_ptr<task_state> source;
    /// @brief Original node/operation exception retained for coordinator observation.
    std::exception_ptr error;
};

/// @brief Prepared prerequisite-to-successor edge with stable failure ordering and retired error
/// storage.
struct dependency_link
{
    /// @brief Retained dependent task made eligible when its prerequisite count reaches zero.
    std::shared_ptr<task_state> successor;
    /// @brief Stable registration sequence used when choosing an inherited failure.
    std::size_t order;
    /// @brief Detached work/error ownership destroyed after releasing scheduler locks.
    retired_failure retired;
};

/// Cycle metadata must not revive completed task states under the pool mutex:
/// their stored exception objects may have user-defined cleanup.
struct task_marker
{
    /// @brief Retained task execution or weak cycle reference, according to this enclosing
    /// ownership record.
    std::weak_ptr<task_state> task;
    /// @brief Count/flag of terminal nodes used by aggregate graph completion.
    bool terminal{false};
};

/// @brief Retained direct-task result and join/dependency state; terminal status publishes
/// immutable outcomes.
struct task_state
{
    /// @brief Weak runtime reference or shared pool used by the enclosing workload/handle.
    std::weak_ptr<scheduler> runtime;
    /// @brief Unique retained origin token that survives OS thread-ID reuse.
    std::shared_ptr<const thread_identity> creator_thread;
    /// @brief Weak source scope used for notification ownership validation.
    std::weak_ptr<task_state> creator;
    /// @brief Whether this task participates in its parent's descendant join.
    bool scoped_child{false};
    /// @brief Whether this task represents delivery of a source completion notification.
    bool observes_completion{false};
    /// @brief Whether the origin exited before its notification could be delivered.
    bool delivery_abandoned{false};
    /// @brief Whether the callback executed through its caller-owned notification queue.
    bool caller_thread{false};
    /// @brief Original failure caused by abandoning origin-affine notification delivery.
    std::exception_ptr delivery_error;
    /// @brief Retained origin mailbox for affine notification dispatch.
    std::shared_ptr<callback_mailbox> target;
    /// @brief Owned or call-scoped diagnostic name, according to the enclosing observation
    /// contract.
    std::string name;
    /// @brief Diagnostic category, or grouped borrowed record views; categories do not imply
    /// priority.
    std::string category;
    /// @brief Runtime-local task identity used for diagnostic correlation.
    std::uint64_t id{0};
    /// @brief Monotonic runtime-local dispatch order for observational scheduling diagnostics.
    std::uint64_t dispatch_index{0};
    /// @brief Explicit policy/configuration retained for this execution or subsystem.
    job_options options;
    /// @brief Prepared cancellation cause retained without constructing it during terminal
    /// publication.
    std::exception_ptr cancellation_error;
    /// @brief Whether this task's own body was suppressed or cooperatively cancelled.
    bool own_cancelled{false};
    /// @brief Whether cancellation prevented invocation of the task's body.
    bool body_suppressed{false};
    /// @brief Whether a prerequisite/descendant contributed cancellation to this outcome.
    bool inherited_cancelled{false};
    /// @brief Whether final stable outcome selection chose cancellation rather than a real failure.
    bool outcome_cancelled{false};
    /// @brief Original worker slot used for execution attribution.
    std::size_t worker_index{0};
    /// @brief Task-owned temporary resource or its terminal allocation statistics.
    std::unique_ptr<scratch_resource> scratch;
    /// @brief Terminal scratch high-water/call statistics retained after storage reclamation.
    scratch_statistics scratch_usage;
    /// @brief Optional bounded task trace or the resulting trace report.
    std::optional<task_trace> trace;
    /// @brief Runtime identity used for foreign-handle validation without owning the runtime.
    std::weak_ptr<const void> owner;
    /// @brief Retained executable work/input; user cleanup is retired outside scheduler locks.
    task_body body;
    /// @brief Current execution status; terminal publication makes the result immutable.
    std::atomic<job_status> status{job_status::pending};
    /// @brief Prepared ready-queue linkage, reused during publication rather than allocated under
    /// locks.
    ready_queue entry;
    /// @brief Prepared successor edges or completion registrations awaiting this task's terminal
    /// result.
    std::list<dependency_link> subscribers;
    /// @brief Weak cycle marker or deterministic fixture lifetime marker in the enclosing state.
    std::shared_ptr<task_marker> marker{std::make_shared<task_marker>()};
    /// @brief Dependency markers used to reject direct/implicit join cycles.
    std::list<std::shared_ptr<task_marker>> waits_for;
    /// @brief Retained parent scope joined by this task, or its diagnostic identity where
    /// specified.
    std::shared_ptr<task_state> parent;
    /// @brief Stable child-registration order used to select deterministic failure causes.
    std::size_t child_order{0};
    /// @brief Next child-registration sequence assigned within this scope.
    std::size_t next_child{0};
    /// @brief Outstanding body/descendant contributions to scoped completion.
    std::size_t unfinished{1};
    /// @brief Unfinished prerequisites; only the transition to zero makes work eligible.
    std::size_t remaining{0};
    /// @brief Earliest registered real failure retained for stable outcome selection.
    std::size_t error_order{std::numeric_limits<std::size_t>::max()};
    /// @brief Whether the originating callback may still use its injected context.
    bool body_open{false};
    /// @brief Whether execution was suppressed, or the total suppressed outcomes in a snapshot.
    bool skipped{false};
    /// @brief Original exception thrown by this task's callback.
    std::exception_ptr own_error;
    /// @brief Stable failure inherited from prerequisites or descendants.
    std::exception_ptr inherited_error;
    /// @brief Original exception selected for the published terminal outcome.
    std::exception_ptr outcome;
    /// @brief Retained source of the stable propagated failure.
    std::shared_ptr<task_state> failure_source;
    /// @brief Deferred parent failure ownership destroyed outside the scheduler lock.
    retired_failure retired_parent;
    /// @brief Submission timestamp or lifetime total of admitted work, as defined by the enclosing
    /// record.
    task_clock::time_point submitted;
    /// @brief Ready-queue admission time/count, as defined by the enclosing event or statistics
    /// type.
    task_clock::time_point queued;
    /// @brief Capture/collection epoch or callback-start timestamp, according to the enclosing
    /// record.
    task_clock::time_point started;
    /// @brief Timestamp when the callback body returned, before descendant joins finish.
    task_clock::time_point body_finished;
    /// @brief Terminal direct-task durations separated by execution phase.
    task_timing timing{};
};

/// @brief Recognize success, failure, skipped and cancelled outcomes as completed states.
inline bool terminal(job_status status)
{
    return status == job_status::succeeded || status == job_status::failed ||
           status == job_status::skipped || status == job_status::cancelled;
}

/// Prepared storage survives the publication lock, including retired user errors.
struct task_submission
{
    /// @brief Initialize prepared admission storage retained across publication so user cleanup
    /// stays outside pool locks.
    task_submission() = default;
    /// @brief Disable copy construction/assignment to preserve unique lifecycle ownership.
    task_submission(const task_submission&) = delete;
    /// @brief Disable copy construction/assignment to preserve unique lifecycle ownership.
    task_submission& operator=(const task_submission&) = delete;
    /// @brief Retained task execution or weak cycle reference, according to this enclosing
    /// ownership record.
    std::shared_ptr<task_state> task;
    /// @brief Validated prerequisite states retained through publication.
    std::vector<std::shared_ptr<task_state>> dependencies;
    /// @brief Prepared successor edges for allocation-safe admission.
    std::list<dependency_link> links;
    /// @brief Detached dependency edges whose user-owned state is destroyed after unlocking.
    std::list<dependency_link> retired_links;
    /// @brief Prepared weak cycle edge from the joining parent to this child.
    std::list<std::shared_ptr<task_marker>> parent_edge;
};
} // namespace atlas::jobs::detail
