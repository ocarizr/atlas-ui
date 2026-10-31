#pragma once

#include "access_registry.hpp"
#include "priority_queue.hpp"
#include "task_state.hpp"
#include <atlas/jobs/job_system.hpp>

#include <condition_variable>
#include <thread>
#include <vector>

namespace atlas::jobs::detail
{
struct scheduler : std::enable_shared_from_this<scheduler>
{
    explicit scheduler(job_system_options options);
    void start_task(task_state& task, std::size_t worker_index);
    void run(const std::stop_token& stop, ready_queue& work, std::size_t worker_index);
    void finish_legacy(ready_queue& retired, const std::exception_ptr& error);
    void drain_and_stop();
    task_handle submit_task(job_label label, task_body body,
                            std::span<const task_handle> prerequisites,
                            const std::shared_ptr<task_state>& parent, job_options options);
    void queue_task(const std::shared_ptr<task_state>& task);
    void prepare_task(task_submission& prepared, job_label label, task_body body,
                      std::span<const task_handle> prerequisites, job_options options);
    void publish_task(task_submission& prepared, const std::shared_ptr<task_state>& parent);
    void prepare_trace(task_submission& prepared, const std::shared_ptr<task_state>& parent);
    void finish_trace(task_state& task, job_status status, task_clock::time_point now);
    std::exception_ptr invoke_task(const std::shared_ptr<task_state>& task);
    void execute_task(const std::shared_ptr<task_state>& task);
    void complete_task(std::shared_ptr<task_state> task);
    std::shared_ptr<task_state> finalize_task(const std::shared_ptr<task_state>& task);
    void publish_successors(const std::shared_ptr<task_state>& task);
    static void inherit_failure(task_state& destination, const std::shared_ptr<task_state>& source,
                                std::size_t order, retired_failure& retired);

    const std::size_t worker_count;
    const scratch_options scratch_config;
    const std::size_t trace_capacity;
    const task_clock::time_point epoch{task_clock::now()};
    std::vector<task_trace> trace_records;
    std::size_t trace_admitted{0};
    std::size_t trace_dropped{0};
    std::uint64_t next_task_id{1};
    std::shared_ptr<access_registry> sanitizer;
    std::shared_ptr<const void> identity{std::make_shared<const int>(0)};
    mutable std::mutex mutex;
    std::mutex lifecycle_mutex;
    std::condition_variable_any available;
    mutable std::condition_variable idle;
    priority_queue queue;
    std::vector<ready_queue> worker_scratch;
    std::size_t running{0};
    std::size_t submitted{0};
    std::size_t completed{0};
    std::size_t waiting{0};
    std::size_t failed{0};
    std::size_t skipped{0};
    std::size_t cancelled{0};
    bool accepting{true};
    // Workers stop/join before shared scheduling state is destroyed.
    std::vector<std::jthread> workers;
};
} // namespace atlas::jobs::detail
