#include "execution_context.hpp"
#include "scheduler_state.hpp"

namespace atlas::jobs::detail
{
void scheduler::observe_task(const std::shared_ptr<task_state>& task, std::uint64_t parent_id)
{
    const auto previous_observing = observing_job;
    observing_job = true;
    observer->completed(
        {task->id, parent_id, task->name, task->category,
         task->status.load(std::memory_order_acquire), task->options.priority,
         task->caller_thread ? task->creator_thread->value : execution_threads[task->worker_index],
         !task->skipped && !task->body_suppressed && !task->delivery_abandoned, task->submitted,
         task->queued, task->started, task->body_finished, task->submitted + task->timing.total,
         task->outcome});
    observing_job = previous_observing;
    {
        const std::lock_guard lock{mutex};
        --observing;
    }
    available.notify_all();
}
} // namespace atlas::jobs::detail
