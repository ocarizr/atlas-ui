#include <atlas/jobs/job_system.hpp>

#include "execution_context.hpp"
#include "scheduler_state.hpp"

#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

namespace atlas::jobs
{
job_handle::job_handle(std::shared_future<void> completion, std::weak_ptr<const void> owner)
    : completion_{std::move(completion)}, owner_{std::move(owner)}
{
}

job_system::job_system(std::size_t worker_count, job_sanitizer_options sanitizer)
    : job_system{job_system_options{worker_count, sanitizer, {}, 0, {}}}
{
}

job_system::job_system(job_system_options options)
    : impl_{std::make_shared<detail::scheduler>(std::move(options))}
{
}

job_trace_report job_system::trace_report() const
{
    const std::lock_guard lock{impl_->mutex};
    return {impl_->trace_capacity != 0, impl_->worker_count, impl_->trace_dropped,
            impl_->trace_admitted - impl_->trace_records.size(), impl_->trace_records};
}

job_system::~job_system()
{
    try
    {
        shutdown();
    }
    catch (...)
    {
        std::terminate();
    }
}

job_dispatcher job_system::dispatcher() const
{
    return job_dispatcher{impl_};
}

job_sanitizer_report job_system::sanitizer_report() const
{
    return impl_->sanitizer->report();
}

job_handle job_system::submit(std::function<void()> job)
{
    if (!job)
    {
        throw std::invalid_argument{"Cannot submit an empty job"};
    }
    detail::ready_queue prepared;
    auto& task = prepared.emplace_back();
    task.callback = std::move(job);
    task.completion.emplace();
    auto completion = task.completion->get_future().share();
    {
        const std::lock_guard lock{impl_->mutex};
        if (!impl_->accepting)
        {
            throw std::logic_error{"The job system is shutting down"};
        }
        impl_->queue.push(prepared, job_priority::normal);
        ++impl_->submitted;
    }
    impl_->available.notify_one();
    return job_handle{std::move(completion), impl_->identity};
}

graph_handle job_system::submit_graph(const job_graph& graph)
{
    // All callback copies, nodes and queue links are allocated before publication.
    auto state = graph.snapshot();
    {
        const std::lock_guard lock{impl_->mutex};
        if (!impl_->accepting)
        {
            throw std::logic_error{"The job system is shutting down"};
        }
        state->scheduler = impl_->identity;
        const auto roots = state->publish(state, impl_->queue.normal());
        impl_->submitted += state->nodes.size();
        impl_->waiting += state->nodes.size() - roots;
        if (state->nodes.empty())
        {
            state->completion.set_value();
        }
    }
    impl_->available.notify_all();
    return graph_handle{std::move(state)};
}

void job_system::wait(const job_handle& job) const
{
    detail::require_coordinator();
    if (!job.completion_.valid() || job.owner_.lock() != impl_->identity)
    {
        throw std::invalid_argument{"Invalid handle or handle owned by another job system"};
    }
    job.completion_.get();
}

void job_system::wait_idle() const
{
    detail::require_coordinator();
    impl_->wait_for_idle();
}

void job_system::wait(const graph_handle& graph) const
{
    detail::require_coordinator();
    if (!graph.state_ || graph.state_->scheduler.lock() != impl_->identity)
    {
        throw std::invalid_argument{"Invalid graph handle or handle owned by another job system"};
    }
    graph.state_->finished.get();
    // Completion publishes every node's terminal state. Choose a stable failure,
    // independent of which worker happened to finish first.
    for (const auto& node : graph.state_->nodes)
    {
        if (node.error)
        {
            throw graph_error{node.name, node.error};
        }
    }
}

std::size_t job_system::dispatch_callbacks(std::size_t limit)
{
    return impl_->dispatch_callbacks(limit);
}

void job_system::shutdown()
{
    detail::require_coordinator();
    impl_->drain_and_stop();
}

job_statistics job_system::statistics() const
{
    const std::lock_guard lock{impl_->mutex};
    auto sizes = impl_->queue.sizes();
    for (std::size_t index = 0; index < sizes.size(); ++index)
    {
        sizes[index] += impl_->affine_queued[index];
    }
    return {impl_->worker_count,
            sizes[0] + sizes[1] + sizes[2],
            impl_->running,
            impl_->submitted,
            impl_->completed,
            impl_->waiting,
            impl_->failed,
            impl_->skipped,
            impl_->callbacks_dispatched,
            impl_->caller_dispatched,
            impl_->cancelled,
            sizes,
            impl_->queue.dispatched};
}
} // namespace atlas::jobs
