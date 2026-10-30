#pragma once

#include "task_state.hpp"

#include <condition_variable>
#include <thread>
#include <vector>

namespace atlas::jobs::detail
{
struct scheduler : std::enable_shared_from_this<scheduler>
{
    explicit scheduler(std::size_t count);
    void run(const std::stop_token& stop, ready_queue& work);
    void finish_legacy(ready_queue& retired, const std::exception_ptr& error);
    void drain_and_stop();
    task_handle submit_task(std::string name, task_body body,
                            std::span<const task_handle> prerequisites,
                            const std::shared_ptr<task_state>& parent);
    void queue_task(const std::shared_ptr<task_state>& task);
    void prepare_task(task_submission& prepared, std::string name, task_body body,
                      std::span<const task_handle> prerequisites);
    void publish_task(task_submission& prepared, const std::shared_ptr<task_state>& parent);
    void execute_task(const std::shared_ptr<task_state>& task);
    void complete_task(std::shared_ptr<task_state> task);
    std::shared_ptr<task_state> finalize_task(const std::shared_ptr<task_state>& task);
    void publish_successors(const std::shared_ptr<task_state>& task);
    static void inherit_failure(task_state& destination, const std::shared_ptr<task_state>& source,
                                std::size_t order, retired_failure& retired);

    const std::size_t worker_count;
    std::shared_ptr<const void> identity{std::make_shared<const int>(0)};
    mutable std::mutex mutex;
    std::mutex lifecycle_mutex;
    std::condition_variable_any available;
    mutable std::condition_variable idle;
    ready_queue queue;
    std::vector<ready_queue> worker_scratch;
    std::size_t running{0};
    std::size_t submitted{0};
    std::size_t completed{0};
    std::size_t waiting{0};
    std::size_t failed{0};
    std::size_t skipped{0};
    bool accepting{true};
    // Workers stop/join before shared scheduling state is destroyed.
    std::vector<std::jthread> workers;
};
} // namespace atlas::jobs::detail
