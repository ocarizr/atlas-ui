#include <atlas/jobs/job_system.hpp>

#include "graph_state.hpp"

#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

namespace atlas::jobs
{
namespace
{
thread_local bool is_worker = false;

void require_coordinator()
{
    if (is_worker)
    {
        throw std::logic_error{"Jobs must not block a worker with scheduler waits or shutdown"};
    }
}
} // namespace

struct job_system::impl
{
    explicit impl(std::size_t count) : worker_count{count}, worker_scratch(count)
    {
        if (count == 0)
        {
            throw std::invalid_argument{"A job system needs at least one worker"};
        }
        workers.reserve(count);
        for (std::size_t index = 0; index < count; ++index)
        {
            workers.emplace_back([this, index](const std::stop_token& stop)
                                 { run(stop, worker_scratch[index]); });
        }
    }

    void run(const std::stop_token& stop, detail::ready_queue& work)
    {
        is_worker = true;
        while (true)
        {
            {
                std::unique_lock lock{mutex};
                if (!available.wait(lock, stop, [this] { return !queue.empty(); }))
                {
                    break;
                }
                work.splice(work.end(), queue, queue.begin());
                ++running;
                if (const auto& entry = work.front(); entry.graph)
                {
                    const std::lock_guard graph_lock{entry.graph->mutex};
                    entry.graph->nodes[entry.index].status = job_status::running;
                }
            }
            std::exception_ptr error;
            try
            {
                work.front().callback();
            }
            catch (...)
            {
                error = std::current_exception();
            }
            finish(work, error);
            work.clear(); // User captures are destroyed with no scheduler/graph lock held.
        }
        is_worker = false;
    }

    void finish(detail::ready_queue& retired, const std::exception_ptr& error)
    {
        auto& work = retired.front();
        const std::lock_guard lock{mutex};
        --running;
        ++completed;
        failed += error ? 1U : 0U;
        if (work.graph)
        {
            const auto result = work.graph->finish(work.graph, work.index, error,
                                                   {.ready = queue, .retired = retired});
            waiting -= result.released;
            completed += result.skipped;
            skipped += result.skipped;
            if (result.finished)
            {
                work.graph->completion.set_value();
            }
            available.notify_all();
        }
        else if (work.completion)
        {
            if (error)
            {
                work.completion->set_exception(error);
            }
            else
            {
                work.completion->set_value();
            }
        }
        else
        {
            std::terminate(); // Every accepted independent job has a completion promise.
        }
        if (queue.empty() && running == 0 && waiting == 0)
        {
            idle.notify_all();
        }
    }

    void drain_and_stop()
    {
        const std::lock_guard lifecycle_lock{lifecycle_mutex};
        {
            std::unique_lock lock{mutex};
            accepting = false;
            idle.wait(lock, [this] { return queue.empty() && running == 0 && waiting == 0; });
        }
        // jthread requests stop and joins. Stop-aware waits wake parked workers.
        workers.clear();
    }

    const std::size_t worker_count;
    std::shared_ptr<const void> identity{std::make_shared<const int>(0)};
    mutable std::mutex mutex;
    std::mutex lifecycle_mutex;
    std::condition_variable_any available;
    mutable std::condition_variable idle;
    detail::ready_queue queue;
    // std::list may allocate its sentinel. Prepare worker scratch before starting
    // threads, rather than constructing a temporary list for every accepted job.
    std::vector<detail::ready_queue> worker_scratch;
    std::size_t running{0};
    std::size_t submitted{0};
    std::size_t completed{0};
    std::size_t waiting{0};
    std::size_t failed{0};
    std::size_t skipped{0};
    bool accepting{true};
    // Declared last: partially constructed workers stop before shared state dies.
    std::vector<std::jthread> workers;
};

job_handle::job_handle(std::shared_future<void> completion, std::weak_ptr<const void> owner)
    : completion_{std::move(completion)}, owner_{std::move(owner)}
{
}

job_system::job_system(std::size_t worker_count) : impl_{std::make_unique<impl>(worker_count)} {}

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
        impl_->queue.splice(impl_->queue.end(), prepared);
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
        const auto roots = state->publish(state, impl_->queue);
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
    require_coordinator();
    if (!job.completion_.valid() || job.owner_.lock() != impl_->identity)
    {
        throw std::invalid_argument{"Invalid handle or handle owned by another job system"};
    }
    job.completion_.get();
}

void job_system::wait_idle() const
{
    require_coordinator();
    std::unique_lock lock{impl_->mutex};
    impl_->idle.wait(
        lock,
        [this] { return impl_->queue.empty() && impl_->running == 0 && impl_->waiting == 0; });
}

void job_system::wait(const graph_handle& graph) const
{
    require_coordinator();
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

void job_system::shutdown()
{
    require_coordinator();
    impl_->drain_and_stop();
}

job_statistics job_system::statistics() const
{
    const std::lock_guard lock{impl_->mutex};
    return {impl_->worker_count, impl_->queue.size(), impl_->running, impl_->submitted,
            impl_->completed,    impl_->waiting,      impl_->failed,  impl_->skipped};
}
} // namespace atlas::jobs
