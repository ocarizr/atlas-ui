#include <atlas/jobs/job_dispatcher.hpp>

#include "execution_context.hpp"
#include "scheduler_state.hpp"

namespace atlas::jobs
{
job_dispatcher::job_dispatcher(std::weak_ptr<detail::scheduler> scheduler)
    : scheduler_{std::move(scheduler)}
{
}

job_dispatcher job_dispatcher::with_options(job_options options) const
{
    if (options.priority != job_priority::high && options.priority != job_priority::normal &&
        options.priority != job_priority::background)
    {
        throw std::invalid_argument{"Invalid job priority"};
    }
    auto result = *this;
    result.options_ = std::move(options);
    return result;
}

bool job_context::stop_requested() const
{
    return active_task()->options.cancellation.stop_requested();
}

void job_context::throw_if_cancelled() const
{
    if (stop_requested())
    {
        throw job_cancelled{};
    }
}

job_priority job_context::priority() const
{
    return active_task()->options.priority;
}

task_handle job_dispatcher::submit(job_label label, detail::task_body body,
                                   std::span<const task_handle> prerequisites,
                                   const std::shared_ptr<detail::task_state>& parent) const
{
    auto scheduler = scheduler_.lock();
    if (!scheduler)
    {
        throw std::logic_error{"The job runtime is no longer available"};
    }
    return scheduler->submit_task(std::move(label), std::move(body), prerequisites, parent,
                                  parent ? parent->options : options_);
}

task_handle job_dispatcher::fence(job_label label, std::span<const task_handle> prerequisites) const
{
    return submit(std::move(label), [](job_context&) {}, prerequisites);
}

task_handle job_dispatcher::fence(job_label label,
                                  std::initializer_list<task_handle> prerequisites) const
{
    return fence(std::move(label), std::span{prerequisites.begin(), prerequisites.size()});
}

job_context::job_context(std::weak_ptr<detail::scheduler> scheduler,
                         std::weak_ptr<detail::task_state> parent)
    : scheduler_{std::move(scheduler)}, parent_{std::move(parent)}
{
}

task_handle job_context::submit(job_label label, detail::task_body body,
                                std::span<const task_handle> prerequisites) const
{
    auto parent = active_task();
    return job_dispatcher{scheduler_}.submit(std::move(label), std::move(body), prerequisites,
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

std::pmr::memory_resource& job_context::scratch() const
{
    const auto task = active_task();
    const auto scheduler = scheduler_.lock();
    if (!scheduler)
    {
        throw std::logic_error{"The job runtime is no longer available"};
    }
    if (!task->scratch)
    {
        task->scratch = std::make_unique<detail::scratch_resource>(scheduler->scratch_config);
    }
    return *task->scratch;
}

task_handle job_context::continue_task(job_label label, detail::task_body body) const
{
    const task_handle prerequisite{active_task()};
    return job_dispatcher{scheduler_}
        .with_options(prerequisite.state_->options)
        .submit(std::move(label), std::move(body), std::span{&prerequisite, 1});
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
