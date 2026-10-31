#include <atlas/jobs/completion_callbacks.hpp>

#include "callback_mailbox.hpp"
#include "execution_context.hpp"
#include "scheduler_state.hpp"

namespace atlas::jobs
{
bool detail::matches(completion_event event, job_status status) noexcept
{
    switch (event)
    {
        case completion_event::complete:
            return true;
        case completion_event::success:
            return status == job_status::succeeded;
        case completion_event::failure:
            return status == job_status::failed;
        case completion_event::cancelled:
            return status == job_status::cancelled;
        case completion_event::skipped:
            return status == job_status::skipped;
    }
    return false;
}

completion_callbacks::completion_callbacks(std::weak_ptr<detail::scheduler> scheduler,
                                           job_priority priority)
    : scheduler_{std::move(scheduler)}, priority_{priority}
{
}

task_handle completion_callbacks::subscribe(const task_handle& source, job_label label,
                                            detail::callback_body body) const
{
    auto runtime = scheduler_.lock();
    if (!runtime)
    {
        throw std::logic_error{"The job runtime is no longer available"};
    }
    if (!source.state_ || source.state_->owner.lock() != runtime->identity)
    {
        throw std::invalid_argument{"Callbacks need a valid source from this runtime"};
    }
    if (source.state_->creator_thread != detail::current_thread_identity())
    {
        throw std::logic_error{"Register callbacks on the thread that submitted the source"};
    }
    if (source.state_->scoped_child)
    {
        const auto creator = source.state_->creator.lock();
        if (!creator || creator.get() != detail::current_task)
        {
            throw std::logic_error{"Register callbacks inside the source's submitting job"};
        }
    }
    std::shared_ptr<detail::task_state> parent;
    if (source.state_->scoped_child && detail::current_task &&
        detail::current_scheduler == runtime.get())
    {
        parent = detail::current_task->marker->task.lock();
    }
    auto target = runtime->current_mailbox(true);
    const job_options options{parent ? parent->options.priority : priority_, {}};
    return runtime->submit_task(std::move(label), std::move(body), std::span{&source, 1}, parent,
                                options, std::move(target));
}
} // namespace atlas::jobs
