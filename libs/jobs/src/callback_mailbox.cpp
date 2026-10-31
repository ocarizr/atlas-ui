#include "callback_mailbox.hpp"
#include "execution_context.hpp"
#include "scheduler_state.hpp"

namespace atlas::jobs::detail
{
namespace
{
struct callback_owner
{
    callback_owner(std::weak_ptr<scheduler> runtime, std::shared_ptr<callback_mailbox> target)
        : runtime{std::move(runtime)}, target{std::move(target)}
    {
    }
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
    callback_owner(const callback_owner&) = delete;
    callback_owner& operator=(const callback_owner&) = delete;
    std::weak_ptr<scheduler> runtime;
    std::shared_ptr<callback_mailbox> target;
};

thread_local std::list<callback_owner> owners;
} // namespace

std::shared_ptr<const void> current_thread_identity()
{
    thread_local auto identity = std::make_shared<const int>(0);
    return identity;
}

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
