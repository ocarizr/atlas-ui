/**
 * @file
 * @brief Coordinator/worker-affine notification dispatch and cooperative waiting.
 */

#include "callback_mailbox.hpp"
#include "execution_context.hpp"
#include "scheduler_state.hpp"

namespace atlas::jobs::detail
{
/// @brief Check the pool's idle predicate while its scheduler state is protected.
bool scheduler::is_idle() const
{
    return queue.empty() && affine_queued == std::array<std::size_t, 3>{} && running == 0 &&
           waiting == 0 && observing == 0;
}

/// @brief Select weighted ready work, preserving affine notifications and fixed-worker ownership.
void scheduler::take_work(ready_queue& work, std::size_t worker_index)
{
    auto& local = worker_mailboxes[worker_index]->ready;
    if (!local.empty() && (queue.empty() || callback_turn[worker_index]))
    {
        local.take(work);
        const auto group = static_cast<std::size_t>(work.front().task->options.priority);
        --affine_queued[group];
        ++callbacks_dispatched[group];
        callback_turn[worker_index] = 0;
    }
    else
    {
        queue.take(work);
        callback_turn[worker_index] = 1;
    }

    ++running;
    const auto& entry = work.front();
    if (entry.graph)
    {
        const std::lock_guard graph_lock{entry.graph->mutex};
        entry.graph->nodes[entry.index].status = job_status::running;
    }

    if (entry.task)
    {
        start_task(*entry.task, worker_index);
    }
}

/// @brief Take and execute one eligible caller-affine notification outside the scheduler lock.
bool scheduler::dispatch_one(const std::shared_ptr<callback_mailbox>& mailbox)
{
    if (!mailbox)
    {
        return false;
    }

    auto& work = mailbox->executing;
    {
        const std::lock_guard lock{mutex};
        if (mailbox->ready.empty())
        {
            return false;
        }

        mailbox->ready.take(work);
        const auto& task = work.front().task;
        --affine_queued[static_cast<std::size_t>(task->options.priority)];
        ++running;
        ++caller_dispatched;
        ++callbacks_dispatched[static_cast<std::size_t>(task->options.priority)];
        start_task(*task, std::numeric_limits<std::size_t>::max(), true);
    }
    execute_task(work.front().task);
    work.clear();
    return true;
}

/// @brief Deliver up to the requested number of this coordinator's queued notifications.
std::size_t scheduler::dispatch_callbacks(std::size_t limit)
{
    require_coordinator();
    const auto mailbox = current_mailbox(false);
    std::size_t delivered = 0;
    while (delivered < limit && dispatch_one(mailbox))
    {
        ++delivered;
    }

    return delivered;
}

/// @brief Cooperatively pump the caller's mailbox until the requested task becomes terminal.
void scheduler::wait_for(const std::shared_ptr<task_state>& task)
{
    const auto mailbox = current_mailbox(false);
    std::unique_lock lock{mutex};
    while (!terminal(task->status.load(std::memory_order_acquire)))
    {
        if (mailbox && !mailbox->ready.empty())
        {
            lock.unlock();
            dispatch_one(mailbox);
            lock.lock();
        }
        else
        {
            available.wait(lock,
                           [&]
                           {
                               return terminal(task->status.load()) ||
                                      (mailbox && !mailbox->ready.empty());
                           });
        }
    }
}

/// @brief Pump caller notifications while waiting for queues, running work and observers to drain.
void scheduler::wait_for_idle()
{
    const auto mailbox = current_mailbox(false);
    std::unique_lock lock{mutex};
    while (!is_idle())
    {
        if (mailbox && !mailbox->ready.empty())
        {
            lock.unlock();
            dispatch_one(mailbox);
            lock.lock();
        }
        else
        {
            available.wait(lock,
                           [&]
                           {
                               return is_idle() || (mailbox && !mailbox->ready.empty());
                           });
        }
    }
}
} // namespace atlas::jobs::detail
