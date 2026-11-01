/**
 * @file
 * @brief Protected shared scheduler queues, worker state and lifecycle accounting.
 */

#pragma once

#include "access_registry.hpp"
#include "priority_queue.hpp"
#include "task_state.hpp"
#include <atlas/jobs/job_system.hpp>

#include <condition_variable>
#include <thread>
#include <vector>

namespace atlas::jobs::detail
{
/// @brief Protected worker-pool state shared by direct dispatch, legacy graphs and notification
/// delivery.
struct scheduler : std::enable_shared_from_this<scheduler>
{
    /// @brief Initialize protected worker-pool state shared by direct dispatch, legacy graphs and
    /// notification delivery.
    explicit scheduler(job_system_options options);
    /// @brief Record execution-thread identity and callback start timing before running a body.
    void start_task(task_state& task, std::size_t worker_index, bool caller = false);
    /// @brief Look up or create the calling thread's notification mailbox for this runtime.
    std::shared_ptr<callback_mailbox> current_mailbox(bool create);
    /// @brief Retire an exited owner's undelivered notifications without invoking them on another
    /// thread.
    void close_mailbox(const std::shared_ptr<callback_mailbox>& mailbox);
    /// @brief Take and execute one eligible caller-affine notification outside the scheduler lock.
    bool dispatch_one(const std::shared_ptr<callback_mailbox>& mailbox);
    /// @brief Deliver up to the requested number of this coordinator's queued notifications.
    std::size_t dispatch_callbacks(std::size_t limit);
    /// @brief Cooperatively pump the caller's mailbox until the requested task becomes terminal.
    void wait_for(const std::shared_ptr<task_state>& task);
    /// @brief Pump caller notifications while waiting for queues, running work and observers to
    /// drain.
    void wait_for_idle();
    /// @brief Check the pool's idle predicate while its scheduler state is protected.
    bool is_idle() const;
    /// @brief Select weighted ready work, preserving affine notifications and fixed-worker
    /// ownership.
    void take_work(ready_queue& work, std::size_t worker_index);
    /// @brief Link an eligible task into its priority or affine queue using prepared storage.
    void enqueue_task(const std::shared_ptr<task_state>& task);
    /// @brief Run one persistent worker's dispatch loop, parking only while no eligible work is
    /// available.
    void run(const std::stop_token& stop, ready_queue& work, std::size_t worker_index);
    /// @brief Publish legacy callback completion and retire user-owned error state outside
    /// scheduler locks.
    void finish_legacy(ready_queue& retired, const std::exception_ptr& error);
    /// @brief Serialize shutdown, drain accepted work and signal/join persistent workers.
    void drain_and_stop();
    /// @brief Prepare a direct task and publish its prerequisite links as one admission operation.
    task_handle submit_task(job_label label, task_body body,
                            std::span<const task_handle> prerequisites,
                            const std::shared_ptr<task_state>& parent, job_options options,
                            std::shared_ptr<callback_mailbox> target = {});
    /// @brief Make an eligible task runnable without occupying a worker on unmet prerequisites.
    void queue_task(const std::shared_ptr<task_state>& task);
    /// @brief Validate direct-task inputs and allocate publication storage before taking the commit
    /// lock.
    void prepare_task(task_submission& prepared, job_label label, task_body body,
                      std::span<const task_handle> prerequisites, job_options options);
    /// @brief Commit a prepared task and its dependencies while preserving parent join accounting.
    void publish_task(task_submission& prepared, const std::shared_ptr<task_state>& parent);
    /// @brief Reserve bounded task-trace metadata when incremental capture is enabled.
    void prepare_trace(task_submission& prepared, const std::shared_ptr<task_state>& parent);
    /// @brief Commit terminal timing/outcome metadata into the task's admitted trace slot.
    void finish_trace(task_state& task, job_status status, task_clock::time_point now);
    /// @brief Execute a task body and preserve its original failure while retiring access guards.
    std::exception_ptr invoke_task(const std::shared_ptr<task_state>& task);
    /// @brief Run ready work, apply cancellation/failure policy and release its body contribution
    /// to the join.
    void execute_task(const std::shared_ptr<task_state>& task);
    /// @brief Finish task/ancestor joins and perform user cleanup and observation outside scheduler
    /// locks.
    void complete_task(std::shared_ptr<task_state> task);
    /// @brief Deliver immutable terminal metadata outside the pool lock and retire observer-drain
    /// accounting.
    void observe_task(const std::shared_ptr<task_state>& task, std::uint64_t parent_id);
    /// @brief Publish a task's immutable terminal result and release eligible dependents under the
    /// pool lock.
    std::shared_ptr<task_state> finalize_task(const std::shared_ptr<task_state>& task);
    /// @brief Release successor prerequisite counts and retain deterministic failure selection.
    void publish_successors(const std::shared_ptr<task_state>& task);
    /// @brief Merge prerequisite failure/cancellation without replacing an earlier stable real
    /// failure.
    static void inherit_failure(task_state& destination, const std::shared_ptr<task_state>& source,
                                std::size_t order, retired_failure& retired);

    /// @brief Configured persistent worker count; must be positive.
    const std::size_t worker_count;
    /// @brief Validated per-task monotonic storage policy shared by this pool.
    const scratch_options scratch_config;
    /// @brief Maximum admitted direct-task records; zero disables bounded tracing.
    const std::size_t trace_capacity;
    /// @brief Optional shared terminal observer retained while the runtime drains deliveries.
    const std::shared_ptr<job_observer> observer;
    /// @brief Original OS thread IDs indexed by worker slot when observation is enabled.
    std::vector<std::thread::id> execution_threads;
    /// @brief Terminal observer deliveries still contributing to idle/shutdown draining.
    std::size_t observing{0};
    /// @brief Steady-clock origin used to convert captured event timestamps.
    const task_clock::time_point epoch{task_clock::now()};
    /// @brief Bounded owning direct-task records admitted for this capture.
    std::vector<task_trace> trace_records;
    /// @brief Number of direct tasks admitted into bounded trace storage during this runtime.
    std::size_t trace_admitted{0};
    /// @brief Direct tasks not admitted after trace record capacity was exhausted.
    std::size_t trace_dropped{0};
    /// @brief Next runtime-local direct-task identity assigned at admission.
    std::uint64_t next_task_id{1};
    /// @brief Declared-access checker configuration/state; disabled checking does not remove basic
    /// contracts.
    std::shared_ptr<access_registry> sanitizer;
    /// @brief Stable owner token used for builder/runtime identity checks.
    std::shared_ptr<const void> identity{std::make_shared<const int>(0)};
    /// @brief Synchronization for the enclosing shared registry/capture/execution state.
    mutable std::mutex mutex;
    /// @brief Mutex serializing worker-pool shutdown callers.
    std::mutex lifecycle_mutex;
    /// @brief Condition variable waking persistent workers when work or shutdown becomes available.
    std::condition_variable_any available;
    /// @brief Condition variable used to observe accepted streaming operations retiring.
    mutable std::condition_variable idle;
    /// @brief Pending accepted I/O operations or eligible work in the enclosing subsystem.
    priority_queue queue;
    /// @brief Per-worker affine queues preserving notification origin ownership.
    std::vector<std::shared_ptr<callback_mailbox>> worker_mailboxes;
    /// @brief Ready affine-notification counts indexed by priority class.
    std::array<std::size_t, 3> affine_queued{};
    /// @brief Notifications executed by coordinator threads rather than workers.
    std::size_t caller_dispatched{0};
    /// @brief Number of origin-affine notifications dispatched by this execution state.
    std::array<std::size_t, 3> callbacks_dispatched{};
    /// @brief Per-worker turn selection between affine notifications and shared ready work.
    std::vector<unsigned char> callback_turn;
    /// @brief Per-worker reusable ready-entry lists used for dispatch without allocating queue
    /// nodes.
    std::vector<ready_queue> worker_scratch;
    /// @brief Callbacks currently executing on CPU workers.
    std::size_t running{0};
    /// @brief Submission timestamp or lifetime total of admitted work, as defined by the enclosing
    /// record.
    std::size_t submitted{0};
    /// @brief Terminal completion time/count, including suppressed and failed outcomes.
    std::size_t completed{0};
    /// @brief Accepted tasks awaiting dependencies, descendants or scoped cleanup.
    std::size_t waiting{0};
    /// @brief Failed terminal outcomes, including parents inheriting descendant failures.
    std::size_t failed{0};
    /// @brief Whether execution was suppressed, or the total suppressed outcomes in a snapshot.
    std::size_t skipped{0};
    /// @brief Cooperative cancellation outcomes, including joined descendants.
    std::size_t cancelled{0};
    /// @brief Whether new root submissions/requests/messages are still admitted.
    bool accepting{true};
    /// Workers stop/join before shared scheduling state is destroyed.
    std::vector<std::jthread> workers;
};
} // namespace atlas::jobs::detail
