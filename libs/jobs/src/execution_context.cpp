#include "execution_context.hpp"

#include <stdexcept>

namespace atlas::jobs::detail
{
thread_local scheduler* current_scheduler = nullptr;
thread_local std::size_t current_worker_index = 0;
thread_local bool worker_thread = false;
thread_local task_state* current_task = nullptr;

void require_coordinator()
{
    if (worker_thread || current_task)
    {
        throw std::logic_error{
            "Job and completion callbacks must not wait, pump callbacks, or shut down the runtime"};
    }
}
} // namespace atlas::jobs::detail
