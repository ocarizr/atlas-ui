#include <atlas/jobs/task_handle.hpp>

#include "execution_context.hpp"
#include "task_state.hpp"

namespace atlas::jobs
{
task_handle::task_handle(std::shared_ptr<detail::task_state> state) : state_{std::move(state)} {}

void task_handle::validate() const
{
    if (!state_)
    {
        throw std::invalid_argument{"Invalid task handle"};
    }
}

job_status task_handle::status() const
{
    validate();
    return state_->status.load(std::memory_order_acquire);
}

bool task_handle::ready() const
{
    return detail::terminal(status());
}

std::string_view task_handle::name() const
{
    validate();
    return state_->name;
}

std::exception_ptr task_handle::exception() const
{
    return ready() ? state_->outcome : std::exception_ptr{};
}

void task_handle::wait() const
{
    detail::require_coordinator();
    validate();
    state_->finished.get();
    if (state_->outcome)
    {
        const auto& source = state_->own_error        ? state_
                             : state_->failure_source ? state_->failure_source
                                                      : state_;
        throw job_error{source->name, state_->outcome};
    }
}

task_timing task_handle::timing() const
{
    if (!ready())
    {
        throw std::logic_error{"Task timing is available after completion"};
    }
    return state_->timing;
}

std::uint64_t task_handle::id() const
{
    validate();
    return state_->id;
}
std::string_view task_handle::category() const
{
    validate();
    return state_->category;
}
scratch_statistics task_handle::scratch_usage() const
{
    if (!ready())
    {
        throw std::logic_error{"Scratch statistics require task completion"};
    }
    return state_->scratch_usage;
}
} // namespace atlas::jobs
