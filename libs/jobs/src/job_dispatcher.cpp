#include <atlas/jobs/job_dispatcher.hpp>

#include "scheduler_state.hpp"

namespace atlas::jobs
{
job_dispatcher::job_dispatcher(std::weak_ptr<detail::scheduler> scheduler)
    : scheduler_{std::move(scheduler)}
{
}

task_handle job_dispatcher::submit(std::string name, detail::task_body body,
                                   std::span<const task_handle> prerequisites,
                                   const std::shared_ptr<detail::task_state>& parent) const
{
    auto scheduler = scheduler_.lock();
    if (!scheduler)
    {
        throw std::logic_error{"The job runtime is no longer available"};
    }
    return scheduler->submit_task(std::move(name), std::move(body), prerequisites, parent);
}

task_handle job_dispatcher::fence(std::string name,
                                  std::span<const task_handle> prerequisites) const
{
    return submit(std::move(name), [](job_context&) {}, prerequisites);
}

task_handle job_dispatcher::fence(std::string name,
                                  std::initializer_list<task_handle> prerequisites) const
{
    return fence(std::move(name), std::span{prerequisites.begin(), prerequisites.size()});
}

job_context::job_context(std::weak_ptr<detail::scheduler> scheduler,
                         std::weak_ptr<detail::task_state> parent)
    : scheduler_{std::move(scheduler)}, parent_{std::move(parent)}
{
}

task_handle job_context::submit(std::string name, detail::task_body body,
                                std::span<const task_handle> prerequisites) const
{
    auto parent = parent_.lock();
    if (!parent)
    {
        throw std::logic_error{"The parent job is no longer available"};
    }
    return job_dispatcher{scheduler_}.submit(std::move(name), std::move(body), prerequisites,
                                             parent);
}
} // namespace atlas::jobs
