#pragma once

#include <atlas/jobs/job_graph.hpp>
#include <atlas/jobs/job_trace.hpp>

#include <chrono>
#include <memory>
#include <string_view>

namespace atlas::jobs
{
using job_error = graph_error;
namespace detail
{
struct task_state;
struct scheduler;
} // namespace detail

struct task_timing
{
    std::chrono::nanoseconds dependency_wait;
    std::chrono::nanoseconds queue_wait;
    std::chrono::nanoseconds execution;
    std::chrono::nanoseconds join;
    std::chrono::nanoseconds total;
};

class task_handle
{
  public:
    task_handle() = default;
    void wait() const;
    [[nodiscard]] bool ready() const;
    [[nodiscard]] job_status status() const;
    [[nodiscard]] std::string_view name() const;
    [[nodiscard]] std::exception_ptr exception() const;
    [[nodiscard]] task_timing timing() const;
    [[nodiscard]] std::uint64_t id() const;
    [[nodiscard]] job_priority priority() const;
    [[nodiscard]] std::string_view category() const;
    [[nodiscard]] scratch_statistics scratch_usage() const;

  private:
    friend struct detail::scheduler;
    friend class job_context;
    explicit task_handle(std::shared_ptr<detail::task_state> state);
    void validate() const;
    std::shared_ptr<detail::task_state> state_;
};
} // namespace atlas::jobs
