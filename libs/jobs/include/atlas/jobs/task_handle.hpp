/**
 * @file
 * @brief Retained direct-task outcomes, timing and coordinator observation.
 */

#pragma once

#include <atlas/jobs/job_graph.hpp>
#include <atlas/jobs/job_trace.hpp>

#include <chrono>
#include <memory>
#include <string_view>

namespace atlas::jobs
{
/// @brief Named direct-task failure retaining the same stable original-cause contract as
/// graph_error.
using job_error = graph_error;

namespace detail
{
/// @brief Retained direct-task result and join/dependency state; terminal status publishes
/// immutable outcomes.
struct task_state;
/// @brief Protected worker-pool state shared by direct dispatch, legacy graphs and notification
/// delivery.
struct scheduler;
} // namespace detail

/// @brief Terminal durations separating prerequisite, ready queue, body and descendant-join costs.
struct task_timing
{
    /// @brief Time between submission and prerequisite eligibility.
    std::chrono::nanoseconds dependency_wait;
    /// @brief Time spent eligible but waiting for execution.
    std::chrono::nanoseconds queue_wait;
    /// @brief Time spent inside the callback body, excluding descendant join time.
    std::chrono::nanoseconds execution;
    /// @brief Time between body return and final scoped completion.
    std::chrono::nanoseconds join;
    /// @brief End-to-end duration from admission through terminal scoped cleanup.
    std::chrono::nanoseconds total;
};

/// @brief Retained direct-task result; owned input is cleaned before terminal publication.
///
/// An empty handle is invalid for observation. Coordinator waits rethrow stable failures and may
/// pump origin-thread notifications; body cleanup precedes terminal publication.
class task_handle
{
  public:
    /// @brief Create an empty handle or retain a direct task's execution result.
    task_handle() = default;
    /// @brief Wait only from a coordinator, publish task effects and rethrow the recorded failure.
    void wait() const;
    /// @brief Report whether the task has published a terminal outcome without waiting.
    [[nodiscard]] bool ready() const;
    /// @brief Read the current execution status; terminal status publishes immutable results.
    [[nodiscard]] job_status status() const;
    /// @brief Borrow the task's diagnostic label for as long as its handle remains alive.
    [[nodiscard]] std::string_view name() const;
    /// @brief Return the recorded original failure when available; observe completion before
    /// consuming results.
    [[nodiscard]] std::exception_ptr exception() const;
    /// @brief Return terminal dependency, queue, execution, join and total durations.
    [[nodiscard]] task_timing timing() const;
    /// @brief Return the runtime-local identity used to correlate direct-task diagnostics.
    [[nodiscard]] std::uint64_t id() const;
    /// @brief Read this task's effective scheduling class; categories do not set priority.
    [[nodiscard]] job_priority priority() const;
    /// @brief Borrow the diagnostic category associated with this task or create its logging
    /// backend.
    [[nodiscard]] std::string_view category() const;
    /// @brief Read terminal scratch allocation statistics after scoped cleanup completes.
    [[nodiscard]] scratch_statistics scratch_usage() const;

  private:
    friend struct detail::scheduler;
    friend class job_context;
    friend class completion_callbacks;
    /// @brief Create an empty handle or retain a direct task's execution result.
    explicit task_handle(std::shared_ptr<detail::task_state> state);
    /// @brief Reject an empty handle before reading task metadata or waiting.
    void validate() const;
    /// @brief Retained execution result/state used by this handle or adapter.
    std::shared_ptr<detail::task_state> state_;
};
} // namespace atlas::jobs
