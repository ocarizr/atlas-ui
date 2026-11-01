/**
 * @file
 * @brief Worker-side invocation, cooperative cancellation and scoped cleanup.
 */

#include "execution_context.hpp"
#include "scheduler_state.hpp"

namespace atlas::jobs::detail
{
/// @brief Execute a task body and preserve its original failure while retiring access guards.
std::exception_ptr scheduler::invoke_task(const std::shared_ptr<task_state>& task)
{
    if (task->delivery_abandoned)
    {
        return task->delivery_error;
    }

    if (task->skipped || task->body_suppressed)
    {
        return task->body_suppressed ? task->cancellation_error : std::exception_ptr{};
    }

    job_context context{weak_from_this(), task};
    std::exception_ptr error;
    auto* previous_task = current_task;
    auto* previous_scheduler = current_scheduler;
    current_scheduler = this;
    current_task = task.get();
    try
    {
        task->body(context);
    }
    catch (const job_cancelled&)
    {
        task->own_cancelled = true;
        error = std::current_exception();
    }
    catch (...)
    {
        error = std::current_exception();
    }

    current_task = previous_task;
    current_scheduler = previous_scheduler;
    try
    {
        if (sanitizer->finish(*task))
        {
            // An ownership violation must not disappear behind cancellation.
            if (!error || task->own_cancelled)
            {
                task->own_cancelled = false;
                error = std::make_exception_ptr(
                    job_sanitizer_error{"A declared access guard escaped its job callback"});
            }
        }
    }
    catch (...)
    {
        if (!error || task->own_cancelled)
        {
            task->own_cancelled = false;
            error = std::current_exception();
        }
    }

    return error;
}

/// @brief Run ready work, apply cancellation/failure policy and release its body contribution to
/// the join.
void scheduler::execute_task(const std::shared_ptr<task_state>& task)
{
    const auto error = invoke_task(task);
    bool finish = false;
    {
        const std::lock_guard lock{mutex};
        task->body_open = false;
        task->own_error = error;
        task->body_finished = task_clock::now();
        task->status.store(job_status::joining);
        --running;
        ++waiting;
        finish = --task->unfinished == 0;
    }
    if (finish)
    {
        complete_task(task);
    }
}
} // namespace atlas::jobs::detail
