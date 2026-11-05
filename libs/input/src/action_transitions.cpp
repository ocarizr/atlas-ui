/** @file @brief Stateful action phases over immutable trigger policies and a caller clock. */
#include "input_state.hpp"

namespace atlas::input
{
/// @brief Raw and semantic events share one hard callback-attempt limit.
void input_system::impl::callback_budget()
{
    if (stats.callbacks == limits.max_callbacks)
    {
        throw std::overflow_error("input callback capacity exhausted");
    }

    ++stats.callbacks;
}

/// @brief A borrowed listener is invoked synchronously on the dispatch owner.
void input_system::impl::emit(action_record& action, action_phase phase, input_value value)
{
    callback_budget();
    const guard delivery{delivering};
    listener->on_action({action.id, action.type, phase, value, stats.frame});
}

/// @brief Sources never invoke application/UI listeners themselves.
void input_system::impl::raw(const event& value)
{
    callback_budget();
    const guard delivery{delivering};
    listener->on_input(value);
}

/// @brief No timer waits: empty dispatches advance pending hold/tap recognition.
void input_system::impl::transition(action_record& a, bool cancelled)
{
    const auto next = a.sum;
    const bool actuated = next != input_value{};
    if (a.active && (a.active_epoch != a.selected_epoch || (cancelled && !actuated)))
    {
        const bool notify = !a.rejected;
        a.active = a.performed = a.rejected = false;
        a.value = {};
        if (notify)
        {
            emit(a, action_phase::cancelled, {});
        }
    }

    if (!actuated)
    {
        if (!a.active)
        {
            a.value = {};
            return;
        }

        const auto old = a.value;
        const auto decision = a.trigger
                                  ? a.trigger->evaluate({time - a.pressed_at, true, a.performed})
                                  : trigger_decision::wait;
        if (decision > trigger_decision::reject)
        {
            throw std::invalid_argument("invalid input trigger decision");
        }

        const bool fire = !a.rejected && !a.performed && decision == trigger_decision::perform;
        const bool complete = !a.rejected && (a.performed || fire);
        const bool reject = !a.rejected && !complete;
        a.active = a.performed = a.rejected = false;
        a.value = {};
        if (fire)
        {
            emit(a, action_phase::performed, old);
        }

        if (complete)
        {
            emit(a, action_phase::completed, {});
        }

        if (reject)
        {
            emit(a, action_phase::cancelled, {});
        }

        return;
    }

    const bool changed = a.value != next;
    a.value = next;
    if (!a.active)
    {
        a.active = true;
        a.performed = a.rejected = false;
        a.pressed_at = time;
        a.active_epoch = a.selected_epoch;
        emit(a, action_phase::started, next);
    }

    if (a.rejected)
    {
        return;
    }

    const auto decision = a.trigger ? a.trigger->evaluate({time - a.pressed_at, false, a.performed})
                                    : trigger_decision::perform;
    if (decision > trigger_decision::reject)
    {
        throw std::invalid_argument("invalid input trigger decision");
    }

    if (decision == trigger_decision::reject)
    {
        a.rejected = true;
        a.performed = false;
        emit(a, action_phase::cancelled, {});
    }
    else if (decision == trigger_decision::perform && (!a.performed || changed))
    {
        a.performed = true;
        emit(a, action_phase::performed, next);
    }
    else if (decision == trigger_decision::wait && !a.performed && a.ongoing_frame != stats.frame)
    {
        a.ongoing_frame = stats.frame;
        emit(a, action_phase::ongoing, next);
    }
}
} // namespace atlas::input
