#include "execution_context.hpp"

#include <stdexcept>

namespace atlas::jobs::detail
{
thread_local bool worker_thread = false;
thread_local task_state* current_task = nullptr;

void require_coordinator()
{
    if (worker_thread)
    {
        throw std::logic_error{"Jobs must not block a worker with scheduler waits or shutdown"};
    }
}
} // namespace atlas::jobs::detail
