#include "execution_context.hpp"
#include "scheduler_state.hpp"

namespace atlas::jobs::detail
{
scheduler::scheduler(std::size_t count) : worker_count{count}, worker_scratch(count)
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

void scheduler::run(const std::stop_token& stop, ready_queue& work)
{
    worker_thread = true;
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
            const auto& entry = work.front();
            if (entry.graph)
            {
                const std::lock_guard graph_lock{entry.graph->mutex};
                entry.graph->nodes[entry.index].status = job_status::running;
            }
            if (entry.task)
            {
                entry.task->started = task_clock::now();
                entry.task->body_open = !entry.task->skipped;
                entry.task->status.store(entry.task->skipped ? job_status::joining
                                                             : job_status::running);
            }
        }
        if (work.front().task)
        {
            execute_task(work.front().task);
        }
        else
        {
            std::exception_ptr error;
            try
            {
                work.front().callback();
            }
            catch (...)
            {
                error = std::current_exception();
            }
            finish_legacy(work, error);
        }
        work.clear();
    }
    worker_thread = false;
}

void scheduler::finish_legacy(ready_queue& retired, const std::exception_ptr& error)
{
    auto& work = retired.front();
    const std::lock_guard lock{mutex};
    --running;
    ++completed;
    failed += error ? 1U : 0U;
    if (work.graph)
    {
        const auto result =
            work.graph->finish(work.graph, work.index, error, {.ready = queue, .retired = retired});
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
        std::terminate();
    }
    if (queue.empty() && running == 0 && waiting == 0)
    {
        idle.notify_all();
    }
}

void scheduler::drain_and_stop()
{
    const std::lock_guard lifecycle_lock{lifecycle_mutex};
    {
        std::unique_lock lock{mutex};
        accepting = false;
        idle.wait(lock, [this] { return queue.empty() && running == 0 && waiting == 0; });
    }
    workers.clear();
}
} // namespace atlas::jobs::detail
