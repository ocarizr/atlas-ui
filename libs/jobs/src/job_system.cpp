#include <atlas/jobs/job_system.hpp>

#include <condition_variable>
#include <deque>
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
    explicit impl(std::size_t count) : worker_count{count}
    {
        if (count == 0)
        {
            throw std::invalid_argument{"A job system needs at least one worker"};
        }
        workers.reserve(count);
        for (std::size_t index = 0; index < count; ++index)
        {
            workers.emplace_back([this](const std::stop_token& stop) { run(stop); });
        }
    }

    void run(const std::stop_token& stop)
    {
        is_worker = true;
        while (true)
        {
            std::packaged_task<void()> work;
            {
                std::unique_lock lock{mutex};
                if (!available.wait(lock, stop, [this] { return !queue.empty(); }))
                {
                    break;
                }
                work = std::move(queue.front());
                queue.pop_front();
                ++running;
            }
            // packaged_task records callback exceptions in the completion future.
            work();
            {
                const std::lock_guard lock{mutex};
                --running;
                ++completed;
                if (queue.empty() && running == 0)
                {
                    idle.notify_all();
                }
            }
        }
        is_worker = false;
    }

    void drain_and_stop()
    {
        const std::lock_guard lifecycle_lock{lifecycle_mutex};
        {
            std::unique_lock lock{mutex};
            accepting = false;
            idle.wait(lock, [this] { return queue.empty() && running == 0; });
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
    std::deque<std::packaged_task<void()>> queue;
    std::size_t running{0};
    std::size_t submitted{0};
    std::size_t completed{0};
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
    std::packaged_task<void()> task{std::move(job)};
    auto completion = task.get_future().share();
    {
        const std::lock_guard lock{impl_->mutex};
        if (!impl_->accepting)
        {
            throw std::logic_error{"The job system is shutting down"};
        }
        impl_->queue.push_back(std::move(task));
        ++impl_->submitted;
    }
    impl_->available.notify_one();
    return job_handle{std::move(completion), impl_->identity};
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
    impl_->idle.wait(lock, [this] { return impl_->queue.empty() && impl_->running == 0; });
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
            impl_->completed};
}
} // namespace atlas::jobs
