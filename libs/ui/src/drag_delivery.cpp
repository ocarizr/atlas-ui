/** @file @brief Weak input leases and bounded retained drag callback delivery. */
#include "drag_state.hpp"

namespace atlas::ui
{
/// @brief Resolve only a live input owner admitted to this window frame.
input_session::impl* drag_drop_session::impl::view(const endpoint& value) const
{
    const auto lease = value.lifetime.lock();
    if (!lease || !lease->owner)
    {
        return nullptr;
    }

    const auto admitted = std::any_of(views.begin(), views.end(),
                                      [&](const auto& weak)
                                      {
                                          return weak.lock() == lease;
                                      });
    auto* state = lease->owner->impl_.get();
    return admitted && !state->stats.closed && !state->stats.failed && state->stage == 2 ? state
                                                                                         : nullptr;
}

/// @brief Retain full identity and handler ownership without borrowing registry storage.
drag_drop_session::impl::endpoint drag_drop_session::impl::capture(input_session& input,
                                                                   element_handle element) const
{
    input.impl_->open();
    const auto* binding = input.impl_->find(element);
    return {input.impl_->lifetime, element, binding ? binding->handler : nullptr};
}

/// @brief Require the same live visible generation and original registration.
bool drag_drop_session::impl::valid(const endpoint& value) const
{
    const auto* state = view(value);
    const auto* binding = state ? state->find(value.target) : nullptr;
    return binding && binding->handler == value.handler && !binding->retiring && !binding->ended &&
           binding->options.enabled && state->eligible(value.target);
}

/// @brief Source reconfiguration or stolen capture invalidates the retained offer.
bool drag_drop_session::impl::valid_source() const
{
    const auto* options = source_options(source);
    const auto* state = view(source);
    const bool capture_owned =
        feedback.phase == drag_phase::armed || (state && state->capture_target == source.target);
    return valid(source) && capture_owned && options && options->payload == offered.payload &&
           options->operations == offered.operations && options->threshold == offered.threshold;
}

/// @brief Resolve the sparse source policy of the admitted owner.
const drag_source_options* drag_drop_session::impl::source_options(const endpoint& value) const
{
    const auto* state = view(value);
    if (!state)
    {
        return nullptr;
    }

    for (const auto& item : state->drag_sources)
    {
        if (item.first == value.target)
        {
            return &item.second;
        }
    }

    return nullptr;
}

/// @brief Resolve the sparse destination policy of the admitted owner.
const drop_target_options* drag_drop_session::impl::target_options(const endpoint& value) const
{
    const auto* state = view(value);
    if (!state)
    {
        return nullptr;
    }

    for (const auto& item : state->drop_targets)
    {
        if (item.first == value.target)
        {
            return &item.second;
        }
    }

    return nullptr;
}

/// @brief Charge the input callback budget and guard serial owner dispatch.
event_response drag_drop_session::impl::emit(const endpoint& value, event_kind kind,
                                             drag_cancel_reason reason)
{
    auto* state = view(value);
    if (!state || !value.handler)
    {
        return {};
    }

    state->check();
    const input_session::impl::guard lock{state->busy};
    if (state->stats.callbacks == state->limits.max_callbacks)
    {
        throw std::overflow_error("UI drag callback limit exhausted");
    }

    ++state->stats.callbacks;
    const auto* region = state->region(value.target);
    routed_event event{kind,
                       route_phase::direct,
                       value.target,
                       value.target,
                       feedback.position,
                       region ? region->transform.local(region->border, feedback.position)
                              : feedback.position,
                       region ? region->bounds : layout_rect{},
                       region ? region->bounds : layout_rect{},
                       region ? region->clip : layout_rect{}};
    event.frame = state->stats.frame;
    event.target_alive = state->tree.contains(value.target);
    event.drag = drag_event{source.target, target.target, payload, feedback.operation,
                            reason,        ticket,        frame};
    const diagnostics::operation_scope scope{sinks, {"ui.drag.callback", "ui.dispatch"}};
    try
    {
        return value.handler->on_event(event, state->tree.commands());
    }
    catch (...)
    {
        state->fail();
        throw;
    }
}

} // namespace atlas::ui
