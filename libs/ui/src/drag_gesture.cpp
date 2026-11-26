/** @file @brief Retained event delivery, capture ownership and cancellation cleanup. */
#include "drag_state.hpp"
#include <exception>

namespace atlas::ui
{
/// @brief Release only this gesture capture before notifying retained handlers.
void drag_drop_session::impl::clear_capture()
{
    auto lease = source.lifetime.lock();
    if (lease && lease->owner)
    {
        auto& state = *lease->owner->impl_;
        if (state.capture_target == source.target)
        {
            state.capture_target = {};
            state.dragging.reset();
            (void)emit(source, event_kind::capture_lost);
        }
    }
}

/// @brief Release gesture payload and handlers without mutating application data.
void drag_drop_session::impl::reset() noexcept
{
    source = {};
    target = {};
    payload = {};
    offered = {};
    accepted = {};
    feedback = {};
    ticket = 0;
    preferred = drag_operation::none;
}

/// @brief Attempt every terminal cleanup while preserving the first callback exception.
void drag_drop_session::impl::cancel(drag_cancel_reason reason)
{
    if (feedback.phase == drag_phase::idle)
    {
        return;
    }

    completion = {ticket, false, reason};
    std::exception_ptr failure;
    const auto attempt = [&](auto&& operation)
    {
        try
        {
            operation();
        }
        catch (...)
        {
            if (!failure)
            {
                failure = std::current_exception();
            }
        }
    };
    attempt(
        [&]
        {
            clear_capture();
        });
    attempt(
        [&]
        {
            (void)emit(target, event_kind::drag_leave, reason);
        });
    attempt(
        [&]
        {
            (void)emit(target, event_kind::drag_cancelled, reason);
        });
    attempt(
        [&]
        {
            (void)emit(source, event_kind::drag_cancelled, reason);
        });
    reset();
    if (failure)
    {
        std::rethrow_exception(failure);
    }
}

/// @brief Cancel uncommitted state without replacing the original exception.
void drag_drop_session::impl::failure() noexcept
{
    diagnostics::write(sinks, diagnostics::log_level::warning, "ui.drag",
                       "Drag input or callback dispatch failed");

    try
    {
        cancel(drag_cancel_reason::callback_failure);
    }
    catch (...)
    {
        reset();
    }
}

/// @brief Prepare one owned offer and acquire the existing input capture after admission.
bool drag_drop_session::impl::start(endpoint value, drag_operation operation, bool keyboard)
{
    if (operation > drag_operation::move)
    {
        throw std::invalid_argument("invalid drag operation");
    }

    const auto* options = source_options(value);
    if (!valid(value) || !options)
    {
        return false;
    }

    const auto policy = *options;
    if (feedback.phase != drag_phase::idle && feedback.phase != drag_phase::armed)
    {
        cancel(drag_cancel_reason::explicit_cancel);
    }

    source = std::move(value);
    offered = policy;
    payload = offered.payload;
    preferred = operation;
    feedback.source = source.target;
    feedback.keyboard = keyboard;
    feedback.phase = drag_phase::dragging;
    const auto response = emit(source, event_kind::drag_prepare);
    if (response.offer)
    {
        payload = response.offer;
    }

    if (!payload)
    {
        cancel(drag_cancel_reason::rejected);
        return false;
    }

    if (!valid(source) || !source_options(source))
    {
        cancel(drag_cancel_reason::source_unavailable);
        return false;
    }

    auto* state = view(source);
    {
        const input_session::impl::guard lock{state->busy};
        state->clear_capture();
    }
    state->capture_target = source.target;
    (void)emit(source, event_kind::pointer_cancel);
    (void)emit(source, event_kind::drag_started);
    if (keyboard)
    {
        next(false);
    }

    return true;
}

/// @brief Create a nonreused transfer ticket without changing application data.
void drag_drop_session::impl::request()
{
    if (feedback.phase != drag_phase::dragging)
    {
        return;
    }

    const auto operation = negotiate(target);
    if (!valid_source() || !valid(target) || operation == drag_operation::none)
    {
        cancel(drag_cancel_reason::rejected);
        return;
    }

    if (serial == std::numeric_limits<std::uint64_t>::max())
    {
        throw std::overflow_error("drag transfer tickets exhausted");
    }

    feedback.operation = operation;
    accepted = *target_options(target);
    ticket = ++serial;
    feedback.phase = drag_phase::pending;
    (void)emit(target, event_kind::drag_drop);
}

/// @brief Require unchanged source and target policies before an application commits.
bool drag_drop_session::impl::preflight(std::uint64_t value) const
{
    const auto* from = source_options(source);
    const auto* to = target_options(target);
    return value && value == ticket && feedback.phase == drag_phase::pending && valid_source() &&
           valid(target) && from && to && from->payload == offered.payload &&
           from->operations == offered.operations && from->threshold == offered.threshold &&
           to->types == accepted.types && to->operations == accepted.operations;
}
} // namespace atlas::ui
