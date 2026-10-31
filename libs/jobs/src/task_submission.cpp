#include "scheduler_state.hpp"

#include <unordered_set>

namespace atlas::jobs::detail
{
namespace
{
bool reaches(const std::shared_ptr<task_state>& start, const task_state* target)
{
    std::vector<std::shared_ptr<task_state>> pending{start};
    std::unordered_set<const task_state*> visited;
    while (!pending.empty())
    {
        auto node = std::move(pending.back());
        pending.pop_back();
        if (node.get() == target)
        {
            return true;
        }
        if (terminal(node->status.load()) || !visited.insert(node.get()).second)
        {
            continue;
        }
        for (const auto& edge : node->waits_for)
        {
            if (!edge->terminal)
            {
                if (auto prerequisite = edge->task.lock())
                {
                    pending.push_back(std::move(prerequisite));
                }
            }
        }
    }
    return false;
}
} // namespace

void scheduler::prepare_task(task_submission& prepared, job_label label, task_body body,
                             std::span<const task_handle> prerequisites, job_options options)
{
    if (label.name.empty() || !body)
    {
        throw std::invalid_argument{"A task needs a nonempty name and function"};
    }
    prepared.task = std::make_shared<task_state>();
    auto& task = prepared.task;
    task->marker->task = task;
    task->name = std::move(label.name);
    task->category = std::move(label.category);
    task->body = std::move(body);
    task->owner = identity;
    task->options = std::move(options);
    if (task->options.cancellation.stop_possible())
    {
        task->cancellation_error = std::make_exception_ptr(job_cancelled{});
    }
    task->entry.emplace_back();
    std::unordered_set<const task_state*> unique;
    for (const auto& handle : prerequisites)
    {
        if (!handle.state_ || handle.state_->owner.lock() != identity)
        {
            throw std::invalid_argument{"Prerequisites must be valid tasks from this runtime"};
        }
        if (unique.insert(handle.state_.get()).second)
        {
            prepared.links.push_back({task, prepared.dependencies.size(), {}});
            prepared.dependencies.push_back(handle.state_);
            task->waits_for.push_back(handle.state_->marker);
        }
    }
}

task_handle scheduler::submit_task(job_label label, task_body body,
                                   std::span<const task_handle> prerequisites,
                                   const std::shared_ptr<task_state>& parent, job_options options)
{
    task_submission prepared;
    prepare_task(prepared, std::move(label), std::move(body), prerequisites, std::move(options));
    prepare_trace(prepared, parent);
    if (parent)
    {
        prepared.parent_edge.push_back(prepared.task->marker);
    }
    {
        const std::lock_guard lock{mutex};
        if (parent)
        {
            if (!parent->body_open)
            {
                throw std::logic_error{"The parent job body has already closed"};
            }
            for (const auto& prerequisite : prepared.dependencies)
            {
                if (reaches(prerequisite, parent.get()))
                {
                    throw std::invalid_argument{"Child dependency would create a completion cycle"};
                }
            }
        }
        else if (!accepting)
        {
            throw std::logic_error{"The job system is shutting down"};
        }
        if (next_task_id == std::numeric_limits<std::uint64_t>::max())
        {
            throw std::overflow_error{"Task identities exhausted"};
        }
        publish_task(prepared, parent);
    }
    available.notify_all();
    return task_handle{std::move(prepared.task)};
}

void scheduler::publish_task(task_submission& prepared, const std::shared_ptr<task_state>& parent)
{
    // Storage and cycle checks are complete: publication cannot allocate.
    const auto& task = prepared.task;
    task->id = next_task_id++;
    if (task->trace)
    {
        if (trace_admitted < trace_capacity)
        {
            ++trace_admitted;
        }
        else
        {
            ++trace_dropped;
            task->trace.reset();
        }
    }
    if (parent)
    {
        task->parent = parent;
        task->child_order = parent->next_child++;
        ++parent->unfinished;
        parent->waits_for.splice(parent->waits_for.end(), prepared.parent_edge);
    }
    task->submitted = task_clock::now();
    for (std::size_t index = 0; index < prepared.dependencies.size(); ++index)
    {
        const auto& prerequisite = prepared.dependencies[index];
        auto& links = prepared.links;
        if (terminal(prerequisite->status.load()))
        {
            inherit_failure(*task, prerequisite, index, links.front().retired);
            prepared.retired_links.splice(prepared.retired_links.end(), links, links.begin());
        }
        else
        {
            ++task->remaining;
            prerequisite->subscribers.splice(prerequisite->subscribers.end(), links, links.begin());
        }
    }
    ++submitted;
    if (task->remaining == 0)
    {
        queue_task(task);
    }
    else
    {
        ++waiting;
    }
}

void scheduler::queue_task(const std::shared_ptr<task_state>& task)
{
    task->skipped = static_cast<bool>(task->inherited_error);
    task->queued = task_clock::now();
    task->status.store(job_status::ready);
    task->entry.front().task = task;
    queue.push(task->entry, task->options.priority);
}
} // namespace atlas::jobs::detail
