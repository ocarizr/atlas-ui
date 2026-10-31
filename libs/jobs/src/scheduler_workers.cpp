#include "callback_mailbox.hpp"
#include "execution_context.hpp"
#include "scheduler_state.hpp"

namespace atlas::jobs::detail
{
scheduler::scheduler(job_system_options options)
    : worker_count{options.worker_count}, scratch_config{options.scratch},
      trace_capacity{options.trace_capacity}, observer{std::move(options.observer)},
      execution_threads(observer ? worker_count : 0),
      sanitizer{std::make_shared<access_registry>(options.sanitizer)}, worker_scratch(worker_count)
{
    const auto count = worker_count;
    if (count == 0)
    {
        throw std::invalid_argument{"A job system needs at least one worker"};
    }
    workers.reserve(count);
    callback_turn.resize(count, 1);
    worker_mailboxes.reserve(count);
    for (std::size_t index = 0; index < count; ++index)
    {
        worker_mailboxes.push_back(std::make_shared<callback_mailbox>());
    }
    scratch_resource validate_scratch{scratch_config};
    trace_records.reserve(trace_capacity);
    for (std::size_t index = 0; index < count; ++index)
    {
        workers.emplace_back([this, index](const std::stop_token& stop)
                             { run(stop, worker_scratch[index], index); });
    }
}

void scheduler::start_task(task_state& task, std::size_t worker_index, bool caller)
{
    task.worker_index = worker_index;
    task.caller_thread = caller;
    task.dispatch_index =
        (caller || (task.target && !task.delivery_abandoned)) ? 0 : queue.dispatch_count;
    task.own_cancelled = !task.skipped && task.options.cancellation.stop_requested();
    task.started = task_clock::now();
    task.body_suppressed = task.own_cancelled;
    task.body_open = !task.skipped && !task.own_cancelled;
    task.status.store((task.skipped || task.own_cancelled) ? job_status::joining
                                                           : job_status::running);
}

void scheduler::run(const std::stop_token& stop, ready_queue& work, std::size_t worker_index)
{
    worker_thread = true;
    current_scheduler = this;
    current_worker_index = worker_index;
    if (observer)
    {
        execution_threads[worker_index] = std::this_thread::get_id();
    }
    const auto& mailbox = worker_mailboxes[worker_index];
    while (true)
    {
        {
            std::unique_lock lock{mutex};
            if (!available.wait(lock, stop,
                                [&] { return !queue.empty() || !mailbox->ready.empty(); }))
            {
                break;
            }
            take_work(work, worker_index);
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
    current_scheduler = nullptr;
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
        const auto result = work.graph->finish(work.graph, work.index, error,
                                               {.ready = queue.normal(), .retired = retired});
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
    if (is_idle())
    {
        idle.notify_all();
    }
    available.notify_all();
}

void scheduler::drain_and_stop()
{
    {
        const std::lock_guard lock{mutex};
        accepting = false;
    }
    wait_for_idle();
    const std::lock_guard lifecycle_lock{lifecycle_mutex};
    workers.clear();
}
} // namespace atlas::jobs::detail
