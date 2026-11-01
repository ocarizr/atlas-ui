/**
 * @file
 * @brief Retained direct-task outcomes, timing and coordinator observation.
 */

#include <atlas/jobs/task_handle.hpp>

#include "execution_context.hpp"
#include "scheduler_state.hpp"
#include "task_state.hpp"

namespace atlas::jobs
{
/// @brief Create an empty handle or retain a direct task's execution result.
task_handle::task_handle(std::shared_ptr<detail::task_state> state) : state_{std::move(state)}
{
}

/// @brief Read this task's effective scheduling class; categories do not set priority.
job_priority task_handle::priority() const
{
    validate();
    return state_->options.priority;
}

/// @brief Reject an empty handle before reading task metadata or waiting.
void task_handle::validate() const
{
    if (!state_)
    {
        throw std::invalid_argument{"Invalid task handle"};
    }
}

/// @brief Read the current execution status; terminal status publishes immutable results.
job_status task_handle::status() const
{
    validate();
    return state_->status.load(std::memory_order_acquire);
}

/// @brief Report whether the task has published a terminal outcome without waiting.
bool task_handle::ready() const
{
    return detail::terminal(status());
}

/// @brief Borrow the task's diagnostic label for as long as its handle remains alive.
std::string_view task_handle::name() const
{
    validate();
    return state_->name;
}

/// @brief Return the recorded original failure when available; observe completion before consuming
/// results.
std::exception_ptr task_handle::exception() const
{
    return ready() ? state_->outcome : std::exception_ptr{};
}

/// @brief Wait only from a coordinator, publish task effects and rethrow the recorded failure.
void task_handle::wait() const
{
    detail::require_coordinator();
    validate();
    // Terminal publication already releases the payload and makes result fields
    // visible. A completed handle needs neither a pool lock nor a second signal.
    if (!ready())
    {
        if (auto runtime = state_->runtime.lock())
        {
            runtime->wait_for(state_);
        }
        else if (!ready())
        {
            throw std::logic_error{"Task runtime expired before terminal publication"};
        }
    }

    if (state_->outcome)
    {
        const auto& source = state_->outcome == state_->own_error ? state_
                             : state_->failure_source             ? state_->failure_source
                                                                  : state_;
        throw job_error{source->name, state_->outcome};
    }
}

/// @brief Return terminal dependency, queue, execution, join and total durations.
task_timing task_handle::timing() const
{
    if (!ready())
    {
        throw std::logic_error{"Task timing is available after completion"};
    }

    return state_->timing;
}

/// @brief Return the runtime-local identity used to correlate direct-task diagnostics.
std::uint64_t task_handle::id() const
{
    validate();
    return state_->id;
}

/// @brief Borrow the diagnostic category associated with this task or create its logging backend.
std::string_view task_handle::category() const
{
    validate();
    return state_->category;
}

/// @brief Read terminal scratch allocation statistics after scoped cleanup completes.
scratch_statistics task_handle::scratch_usage() const
{
    if (!ready())
    {
        throw std::logic_error{"Scratch statistics require task completion"};
    }

    return state_->scratch_usage;
}
} // namespace atlas::jobs
