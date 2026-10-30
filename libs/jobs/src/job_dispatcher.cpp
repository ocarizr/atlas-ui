#include <atlas/jobs/job_dispatcher.hpp>

#include "execution_context.hpp"
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
    auto parent = active_task();
    return job_dispatcher{scheduler_}.submit(std::move(name), std::move(body), prerequisites,
                                             parent);
}

std::shared_ptr<detail::task_state> job_context::active_task() const
{
    auto parent = parent_.lock();
    if (!parent || detail::current_task != parent.get())
    {
        throw std::logic_error{"A job context is usable only inside its originating callback"};
    }
    return parent;
}

job_dispatcher job_context::dispatcher() const
{
    (void)active_task();
    return job_dispatcher{scheduler_};
}

task_handle job_context::continue_task(std::string name, detail::task_body body) const
{
    const task_handle prerequisite{active_task()};
    return job_dispatcher{scheduler_}.submit(std::move(name), std::move(body),
                                             std::span{&prerequisite, 1});
}

access_guard job_context::access(std::span<const memory_access> regions) const
{
    const auto task = active_task();
    const auto scheduler = scheduler_.lock();
    if (!scheduler)
    {
        throw std::logic_error{"The job runtime is no longer available"};
    }
    const auto token = scheduler->sanitizer->acquire(*task, regions);
    return access_guard{scheduler->sanitizer, token};
}

access_guard job_context::access(std::initializer_list<memory_access> regions) const
{
    return access(std::span{regions.begin(), regions.size()});
}

access_guard job_context::access(memory_access region) const
{
    return access(std::span{&region, 1});
}
} // namespace atlas::jobs
