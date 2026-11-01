/**
 * @file
 * @brief Unique thread ownership and mailbox retirement on owner exit.
 */

#include "callback_mailbox.hpp"
#include "execution_context.hpp"
#include "scheduler_state.hpp"

namespace atlas::jobs::detail
{
namespace
{
/// @brief Thread-local mailbox registration that retires undelivered work when its owner exits.
struct callback_owner
{
    /// @brief Initialize thread-local mailbox registration that retires undelivered work when its
    /// owner exits.
    callback_owner(std::weak_ptr<scheduler> runtime, std::shared_ptr<callback_mailbox> target)
        : runtime{std::move(runtime)}, target{std::move(target)}
    {
    }

    /// @brief Release callback owner resources and complete its scoped cleanup.
    ~callback_owner()
    {
        try
        {
            if (auto pool = runtime.lock())
            {
                pool->close_mailbox(target);
            }
        }
        catch (...)
        {
            std::terminate();
        }
    }

    /// @brief Disable copy construction/assignment to preserve unique lifecycle ownership.
    callback_owner(const callback_owner&) = delete;
    /// @brief Disable copy construction/assignment to preserve unique lifecycle ownership.
    callback_owner& operator=(const callback_owner&) = delete;
    /// @brief Weak runtime reference or shared pool used by the enclosing workload/handle.
    std::weak_ptr<scheduler> runtime;
    /// @brief Retained origin mailbox for affine notification dispatch.
    std::shared_ptr<callback_mailbox> target;
};

/// @brief Per-thread registrations that retire undelivered notifications when their owner exits.
thread_local std::list<callback_owner> owners;
} // namespace

/// @brief Retain a unique owner token so recycled OS thread IDs cannot claim old notification
/// queues.
std::shared_ptr<const thread_identity> current_thread_identity()
{
    thread_local auto identity = std::make_shared<const thread_identity>();
    return identity;
}

/// @brief Look up or create the calling thread's notification mailbox for this runtime.
std::shared_ptr<callback_mailbox> scheduler::current_mailbox(bool create)
{
    if (worker_thread)
    {
        if (current_scheduler != this || !current_task)
        {
            throw std::logic_error{"Register callbacks inside a job of this runtime"};
        }

        return worker_mailboxes[current_worker_index];
    }

    for (auto owner = owners.begin(); owner != owners.end();)
    {
        const auto pool = owner->runtime.lock();
        if (!pool)
        {
            owner = owners.erase(owner);
        }
        else if (pool.get() == this)
        {
            return owner->target;
        }
        else
        {
            ++owner;
        }
    }

    if (!create)
    {
        return {};
    }

    auto target = std::make_shared<callback_mailbox>();
    owners.emplace_back(weak_from_this(), target);
    return target;
}

/// @brief Link an eligible task into its priority or affine queue using prepared storage.
void scheduler::enqueue_task(const std::shared_ptr<task_state>& task)
{
    if (task->target && !task->target->closed)
    {
        ++affine_queued[static_cast<std::size_t>(task->options.priority)];
        task->target->ready.push(task->entry, task->options.priority);
    }
    else
    {
        task->delivery_abandoned = static_cast<bool>(task->target);
        queue.push(task->entry, task->options.priority);
    }
}

/// @brief Retire an exited owner's undelivered notifications without invoking them on another
/// thread.
void scheduler::close_mailbox(const std::shared_ptr<callback_mailbox>& mailbox)
{
    const std::lock_guard lock{mutex};
    mailbox->closed = true;
    auto& entries = mailbox->transfer;
    while (!mailbox->ready.empty())
    {
        mailbox->ready.take(entries);
        const auto& task = entries.front().task;
        --affine_queued[static_cast<std::size_t>(task->options.priority)];
        task->delivery_abandoned = true;
        queue.push(entries, task->options.priority);
    }

    available.notify_all();
}
} // namespace atlas::jobs::detail
