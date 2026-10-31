#include "execution_context.hpp"
#include "scheduler_state.hpp"

namespace atlas::jobs::detail
{
void scheduler::complete_task(std::shared_ptr<task_state> task)
{
    while (task)
    {
        auto current = std::move(task);
        // Keep the parent's payload alive until every child is terminal. User
        // cleanup runs outside scheduler locks and before publishing completion.
        task_body retired;
        retired.swap(current->body);
        retired = {};
        if (current->scratch)
        {
            current->scratch_usage = current->scratch->statistics();
            current->scratch.reset();
        }
        {
            const std::lock_guard lock{mutex};
            task = finalize_task(current);
        }
        current->subscribers.clear();
        current->retired_parent = {};
    }
}

void scheduler::inherit_failure(task_state& destination, const std::shared_ptr<task_state>& source,
                                std::size_t order, retired_failure& retired)
{
    const bool same_kind = destination.inherited_cancelled == source->outcome_cancelled;
    const bool prefer_failure = destination.inherited_cancelled && !source->outcome_cancelled;
    if (source->outcome && (!destination.inherited_error || prefer_failure ||
                            (same_kind && order < destination.error_order)))
    {
        retired.error = destination.inherited_error;
        retired.source = std::move(destination.failure_source);
        destination.error_order = order;
        destination.inherited_error = source->outcome;
        destination.inherited_cancelled = source->outcome_cancelled;
        destination.failure_source = source->outcome == source->own_error ? source
                                     : source->failure_source             ? source->failure_source
                                                                          : source;
    }
}

void scheduler::publish_successors(const std::shared_ptr<task_state>& task)
{
    for (auto& link : task->subscribers)
    {
        const auto& successor = link.successor;
        inherit_failure(*successor, task, link.order, link.retired);
        if (--successor->remaining == 0)
        {
            --waiting;
            queue_task(successor);
        }
    }
}

std::shared_ptr<task_state> scheduler::finalize_task(const std::shared_ptr<task_state>& task)
{
    auto parent = std::move(task->parent);
    // Real failures outrank cooperative cancellation, then registration order.
    const bool use_own = task->own_error && (!task->own_cancelled || !task->inherited_error ||
                                             task->inherited_cancelled);
    task->outcome = use_own ? task->own_error : task->inherited_error;
    task->outcome_cancelled = use_own ? task->own_cancelled : task->inherited_cancelled;
    const auto now = task_clock::now();
    const auto nanoseconds = [](task_clock::duration duration)
    { return std::chrono::duration_cast<std::chrono::nanoseconds>(duration); };
    task->timing = {
        nanoseconds(task->queued - task->submitted), nanoseconds(task->started - task->queued),
        (task->skipped || task->body_suppressed) ? std::chrono::nanoseconds::zero()
                                                 : nanoseconds(task->body_finished - task->started),
        nanoseconds(now - task->body_finished), nanoseconds(now - task->submitted)};
    const auto outcome = task->skipped             ? job_status::skipped
                         : task->outcome_cancelled ? job_status::cancelled
                         : task->outcome           ? job_status::failed
                                                   : job_status::succeeded;
    finish_trace(*task, outcome, now);
    --waiting;
    ++completed;
    failed += outcome == job_status::failed ? 1U : 0U;
    skipped += outcome == job_status::skipped ? 1U : 0U;
    cancelled += outcome == job_status::cancelled ? 1U : 0U;
    task->marker->terminal = true;
    task->waits_for.clear();
    task->status.store(outcome, std::memory_order_release);
    task->completion.set_value();
    publish_successors(task);
    std::shared_ptr<task_state> next;
    if (parent)
    {
        inherit_failure(*parent, task, task->child_order, task->retired_parent);
        if (--parent->unfinished == 0)
        {
            next = std::move(parent);
        }
    }
    available.notify_all();
    if (queue.empty() && running == 0 && waiting == 0)
    {
        idle.notify_all();
    }
    return next;
}
} // namespace atlas::jobs::detail
