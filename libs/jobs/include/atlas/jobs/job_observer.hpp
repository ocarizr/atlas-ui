#pragma once
#include <atlas/jobs/job_graph.hpp>
#include <atlas/jobs/job_policy.hpp>

#include <chrono>
#include <cstdint>
#include <exception>
#include <string_view>
#include <thread>

namespace atlas::jobs
{
struct task_event
{
    std::uint64_t id, parent_id;
    std::string_view name, category;
    job_status status;
    job_priority priority;
    std::thread::id execution_thread;
    bool body_executed;
    std::chrono::steady_clock::time_point submitted, queued, started, body_finished, completed;
    std::exception_ptr exception;
};

// Borrowed metadata is valid for this call only. Implementations must not throw,
// block on external work, wait on jobs, or retain borrowed views.
class job_observer
{
  public:
    virtual ~job_observer() = default;
    virtual void completed(const task_event& event) noexcept = 0;
};
} // namespace atlas::jobs
