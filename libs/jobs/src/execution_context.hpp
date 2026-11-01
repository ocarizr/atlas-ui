/**
 * @file
 * @brief Thread-local executor identity and restrictions on blocking/reentrant operations.
 */

#pragma once

#include <cstddef>

namespace atlas::jobs::detail
{
/// @brief Protected worker-pool state shared by direct dispatch, legacy graphs and notification
/// delivery.
struct scheduler;
/// @brief Pool currently executing on this thread, used to validate context ownership.
extern thread_local scheduler* current_scheduler;
/// @brief Current worker slot while executing worker-owned work; ignored outside that context.
extern thread_local std::size_t current_worker_index;
/// @brief Whether this thread belongs to the persistent CPU pool and must not perform worker waits.
extern thread_local bool worker_thread;
/// @brief Whether terminal observation is running, including on a coordinator that must not wait.
extern thread_local bool observing_job;
/// @brief Retained direct-task result and join/dependency state; terminal status publishes
/// immutable outcomes.
struct task_state;
/// @brief Currently executing callback scope; saved contexts cannot substitute another scope.
extern thread_local task_state* current_task;
/// @brief Reject blocking lifecycle operations from workers, executing callbacks and terminal
/// observers.
void require_coordinator();
} // namespace atlas::jobs::detail
