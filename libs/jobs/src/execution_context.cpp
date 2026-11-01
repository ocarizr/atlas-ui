/**
 * @file
 * @brief Thread-local executor identity and restrictions on blocking/reentrant operations.
 */

#include "execution_context.hpp"

#include <stdexcept>

namespace atlas::jobs::detail
{
/// @brief Pool currently executing on this thread, used to validate context ownership.
thread_local scheduler* current_scheduler = nullptr;
/// @brief Current worker slot while executing worker-owned work; ignored outside that context.
thread_local std::size_t current_worker_index = 0;
/// @brief Whether this thread belongs to the persistent CPU pool and must not perform worker waits.
thread_local bool worker_thread = false;
/// @brief Whether terminal observation is running, including on a coordinator that must not wait.
thread_local bool observing_job = false;
/// @brief Currently executing callback scope; saved contexts cannot substitute another scope.
thread_local task_state* current_task = nullptr;

/// @brief Reject blocking lifecycle operations from workers, executing callbacks and terminal
/// observers.
void require_coordinator()
{
    if (worker_thread || current_task || observing_job)
    {
        throw std::logic_error{
            "Job and completion callbacks must not wait, pump callbacks, or shut down the runtime"};
    }
}
} // namespace atlas::jobs::detail
