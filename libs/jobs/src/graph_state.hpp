#pragma once

#include <atlas/jobs/job_graph.hpp>

#include <future>
#include <list>
#include <mutex>
#include <optional>
#include <vector>

namespace atlas::jobs::detail
{
struct task_state;
struct scheduled_job
{
    std::function<void()> callback;
    std::optional<std::promise<void>> completion;
    // Pending entries do not own their graph. Ready/running entries do.
    std::shared_ptr<graph_state> graph;
    std::size_t index{0};
    std::shared_ptr<task_state> task;
};

using ready_queue = std::list<scheduled_job>;

struct queue_transition
{
    ready_queue& ready;
    ready_queue& retired;
};

struct graph_node
{
    std::string name;
    std::vector<std::size_t> successors;
    std::size_t remaining{0};
    bool blocked{false};
    job_status status{job_status::pending};
    std::exception_ptr error;
    ready_queue::iterator entry;
};

struct graph_completion
{
    std::size_t released{0};
    std::size_t skipped{0};
    bool finished{false};
};

struct graph_state
{
    std::size_t publish(const std::shared_ptr<graph_state>& self, ready_queue& ready);
    graph_completion finish(const std::shared_ptr<graph_state>& self, std::size_t index,
                            const std::exception_ptr& error, queue_transition queues);
    void make_ready(const std::shared_ptr<graph_state>& self, std::size_t index,
                    ready_queue& ready);

    // Scheduler lock precedes this mutex; diagnostic readers only take this mutex.
    mutable std::mutex mutex;
    std::shared_ptr<const void> identity;
    std::weak_ptr<const void> scheduler;
    std::vector<graph_node> nodes;
    ready_queue pending;
    std::vector<std::size_t> propagation;
    std::size_t terminal{0};
    std::promise<void> completion;
    std::shared_future<void> finished{completion.get_future().share()};
};
} // namespace atlas::jobs::detail
