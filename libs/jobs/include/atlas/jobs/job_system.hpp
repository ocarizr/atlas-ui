#pragma once

#include <atlas/jobs/job_dispatcher.hpp>
#include <atlas/jobs/job_graph.hpp>
#include <atlas/jobs/job_observer.hpp>

#include <array>
#include <cstddef>
#include <functional>
#include <future>
#include <limits>
#include <memory>

namespace atlas::jobs
{
struct job_system_options
{
    std::size_t worker_count{1};
    job_sanitizer_options sanitizer;
    scratch_options scratch;
    std::size_t trace_capacity{0};
    std::shared_ptr<job_observer> observer;
};

class job_system;

class job_handle
{
  public:
    job_handle() = default;

  private:
    friend class job_system;
    job_handle(std::shared_future<void> completion, std::weak_ptr<const void> owner);

    std::shared_future<void> completion_;
    std::weak_ptr<const void> owner_;
};

struct job_statistics
{
    std::size_t workers;
    std::size_t queued;
    std::size_t running;
    std::size_t submitted;
    std::size_t completed; // Terminal jobs, including failed and skipped jobs.
    std::size_t waiting;   // Jobs awaiting prerequisites, children, or cleanup.
    std::size_t failed;    // Failed outcomes, including parents with failed children.
    std::size_t skipped;   // Callbacks suppressed by failed/cancelled prerequisites.
    std::array<std::size_t, 3> callbacks_dispatched_by_priority;
    std::size_t caller_dispatched; // Notification tasks executed by caller threads.
    std::size_t cancelled; // Cancelled outcomes, including parents joining cancelled children.
    std::array<std::size_t, 3> queued_by_priority;
    std::array<std::size_t, 3> dispatched_by_priority;
};

// Jobs must not call wait, wait_idle, shutdown, or destroy a scheduler.
class job_system
{
  public:
    explicit job_system(std::size_t worker_count, job_sanitizer_options sanitizer = {});
    explicit job_system(job_system_options options);
    ~job_system();
    job_system(const job_system&) = delete;
    job_system& operator=(const job_system&) = delete;
    job_system(job_system&&) = delete;
    job_system& operator=(job_system&&) = delete;

    job_handle submit(std::function<void()> job);
    [[nodiscard]] job_dispatcher dispatcher() const;
    // Validates and snapshots the entire graph before publishing any work.
    graph_handle submit_graph(const job_graph& graph);
    void wait(const job_handle& job) const;
    void wait(const graph_handle& graph) const;
    void wait_idle() const;
    std::size_t dispatch_callbacks(std::size_t limit = std::numeric_limits<std::size_t>::max());
    void shutdown();
    [[nodiscard]] job_statistics statistics() const;
    [[nodiscard]] job_sanitizer_report sanitizer_report() const;
    [[nodiscard]] job_trace_report trace_report() const;

  private:
    std::shared_ptr<detail::scheduler> impl_;
};
} // namespace atlas::jobs
