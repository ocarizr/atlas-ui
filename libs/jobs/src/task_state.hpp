#pragma once

#include "graph_state.hpp"
#include <atlas/jobs/job_dispatcher.hpp>

#include <atomic>
#include <chrono>
#include <limits>

namespace atlas::jobs::detail
{
using task_clock = std::chrono::steady_clock;

struct retired_failure
{
    std::shared_ptr<task_state> source;
    std::exception_ptr error;
};

struct dependency_link
{
    std::shared_ptr<task_state> successor;
    std::size_t order;
    retired_failure retired;
};

// Cycle metadata must not revive completed task states under the pool mutex:
// their stored exception objects may have user-defined cleanup.
struct task_marker
{
    std::weak_ptr<task_state> task;
    bool terminal{false};
};

struct task_state
{
    std::string name;
    std::weak_ptr<const void> owner;
    task_body body;
    std::atomic<job_status> status{job_status::pending};
    std::promise<void> completion;
    std::shared_future<void> finished{completion.get_future().share()};
    ready_queue entry;
    std::list<dependency_link> subscribers;
    std::shared_ptr<task_marker> marker{std::make_shared<task_marker>()};
    std::list<std::shared_ptr<task_marker>> waits_for;
    std::shared_ptr<task_state> parent;
    std::size_t child_order{0};
    std::size_t next_child{0};
    std::size_t unfinished{1};
    std::size_t remaining{0};
    std::size_t error_order{std::numeric_limits<std::size_t>::max()};
    bool body_open{false};
    bool skipped{false};
    std::exception_ptr own_error;
    std::exception_ptr inherited_error;
    std::exception_ptr outcome;
    std::shared_ptr<task_state> failure_source;
    retired_failure retired_parent;
    task_clock::time_point submitted;
    task_clock::time_point queued;
    task_clock::time_point started;
    task_clock::time_point body_finished;
    task_timing timing{};
};

inline bool terminal(job_status status)
{
    return status == job_status::succeeded || status == job_status::failed ||
           status == job_status::skipped;
}

// Prepared storage survives the publication lock, including retired user errors.
struct task_submission
{
    task_submission() = default;
    task_submission(const task_submission&) = delete;
    task_submission& operator=(const task_submission&) = delete;
    std::shared_ptr<task_state> task;
    std::vector<std::shared_ptr<task_state>> dependencies;
    std::list<dependency_link> links;
    std::list<dependency_link> retired_links;
    std::list<std::shared_ptr<task_marker>> parent_edge;
};
} // namespace atlas::jobs::detail
