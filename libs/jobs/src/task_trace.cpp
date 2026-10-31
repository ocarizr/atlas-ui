#include "scheduler_state.hpp"

namespace atlas::jobs::detail
{
void scheduler::prepare_trace(task_submission& prepared, const std::shared_ptr<task_state>& parent)
{
    auto& task = prepared.task;
    if (parent && task->category.empty())
    {
        task->category = parent->category;
    }
    if (trace_capacity == 0)
    {
        return;
    }
    task_trace record;
    record.name = task->name;
    record.category = task->category;
    record.parent_id = parent ? parent->id : 0;
    record.prerequisites.reserve(prepared.dependencies.size());
    for (const auto& prerequisite : prepared.dependencies)
    {
        record.prerequisites.push_back(prerequisite->id);
    }
    task->trace.emplace(std::move(record));
}

void scheduler::finish_trace(task_state& task, job_status status, task_clock::time_point now)
{
    if (!task.trace)
    {
        return;
    }
    auto& record = *task.trace;
    const auto offset = [this](task_clock::time_point time)
    { return std::chrono::duration_cast<std::chrono::nanoseconds>(time - epoch); };
    record.id = task.id;
    record.worker_index = task.worker_index;
    record.status = status;
    record.submitted = offset(task.submitted);
    record.started = offset(task.started);
    record.body_finished = offset(task.body_finished);
    record.completed = offset(now);
    record.scratch = task.scratch_usage;
    trace_records.push_back(std::move(record));
}
} // namespace atlas::jobs::detail
