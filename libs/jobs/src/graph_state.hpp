/**
 * @file
 * @brief Sealed-graph eligibility publication and stable failure propagation.
 */

#pragma once

#include <atlas/jobs/job_graph.hpp>

#include <future>
#include <list>
#include <mutex>
#include <optional>
#include <vector>

namespace atlas::jobs::detail
{
/// @brief Retained direct-task result and join/dependency state; terminal status publishes
/// immutable outcomes.
struct task_state;

/// @brief Ready-queue entry carrying either legacy callback/graph state or a direct task.
struct scheduled_job
{
    /// @brief User-owned graph/legacy callback, invoked only after prerequisites become eligible.
    std::function<void()> callback;
    /// @brief Retained callback/promise making the accepted operation observable to its
    /// coordinator.
    std::optional<std::promise<void>> completion;
    /// Pending entries do not own their graph. Ready/running entries do.
    std::shared_ptr<graph_state> graph;
    /// @brief Graph/range-local index validated against its enclosing storage.
    std::size_t index{0};
    /// @brief Retained task execution or weak cycle reference, according to this enclosing
    /// ownership record.
    std::shared_ptr<task_state> task;
};

/// @brief Spliceable prepared queue nodes reused during eligibility publication.
using ready_queue = std::list<scheduled_job>;

/// @brief Borrowed ready/retired queues used to publish graph successors without destroying user
/// state in locks.
struct queue_transition
{
    /// @brief Borrowed ready queue receiving eligible graph nodes.
    ready_queue& ready;
    /// @brief Detached work/error ownership destroyed after releasing scheduler locks.
    ready_queue& retired;
};

/// @brief Validated execution-node state with prerequisite accounting and prepared queue linkage.
struct graph_node
{
    /// @brief Owned or call-scoped diagnostic name, according to the enclosing observation
    /// contract.
    std::string name;
    /// @brief Dependent node indices released by prerequisite completion.
    std::vector<std::size_t> successors;
    /// @brief Unfinished prerequisites; only the transition to zero makes work eligible.
    std::size_t remaining{0};
    /// @brief Whether a failed prerequisite suppresses this node's callback.
    bool blocked{false};
    /// @brief Current execution status; terminal publication makes the result immutable.
    job_status status{job_status::pending};
    /// @brief Original node/operation exception retained for coordinator observation.
    std::exception_ptr error;
    /// @brief Prepared ready-queue linkage, reused during publication rather than allocated under
    /// locks.
    ready_queue::iterator entry;
};

/// @brief Result of one sealed-graph terminal transition, including newly eligible and skipped
/// nodes.
struct graph_completion
{
    /// @brief Successor nodes made eligible by a terminal graph transition.
    std::size_t released{0};
    /// @brief Whether execution was suppressed, or the total suppressed outcomes in a snapshot.
    std::size_t skipped{0};
    /// @brief Whether every node is now terminal and aggregate completion can be published.
    bool finished{false};
};

/// @brief Retained immutable graph topology plus synchronized execution results and aggregate
/// completion.
struct graph_state
{
    /// @brief Publish prepared zero-prerequisite graph nodes into the ready queue.
    std::size_t publish(const std::shared_ptr<graph_state>& self, ready_queue& ready);
    /// @brief Commit one node result, propagate blocked successors and complete the aggregate graph
    /// when terminal.
    graph_completion finish(const std::shared_ptr<graph_state>& self, std::size_t index,
                            const std::exception_ptr& error, queue_transition queues);
    /// @brief Splice a prepared eligible node into the ready queue while retaining its graph
    /// execution.
    void make_ready(const std::shared_ptr<graph_state>& self, std::size_t index,
                    ready_queue& ready);

    /// Scheduler lock precedes this mutex; diagnostic readers only take this mutex.
    mutable std::mutex mutex;
    /// @brief Stable owner token used for builder/runtime identity checks.
    std::shared_ptr<const void> identity;
    /// @brief Weak runtime identity used to reject foreign execution handles.
    std::weak_ptr<const void> scheduler;
    /// @brief Owned graph nodes with immutable accepted topology and synchronized results.
    std::vector<graph_node> nodes;
    /// @brief Prepared graph entries awaiting eligibility publication into the scheduler queue.
    ready_queue pending;
    /// @brief Prepared traversal storage used to propagate graph failures without new queue
    /// allocation.
    std::vector<std::size_t> propagation;
    /// @brief Count/flag of terminal nodes used by aggregate graph completion.
    std::size_t terminal{0};
    /// @brief Retained callback/promise making the accepted operation observable to its
    /// coordinator.
    std::promise<void> completion;
    /// @brief Aggregate completion signal or terminal flag used by the enclosing operation.
    std::shared_future<void> finished{completion.get_future().share()};
};
} // namespace atlas::jobs::detail
