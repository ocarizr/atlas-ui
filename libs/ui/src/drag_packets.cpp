/** @file @brief Nonblocking pointer, portable keyboard and semantic drag input. */
#include "drag_state.hpp"
#include <cmath>

namespace atlas::ui
{
/// @brief Map portable keys and semantic actions without device polling or worker callbacks.
bool drag_drop_session::impl::keyboard(input_event event)
{
    const bool key_down = keys && event.kind == input_kind::key_down;
    const bool key_up = keys && event.kind == input_kind::key_up;
    const auto key = static_cast<input::key>(event.key);
    if ((key_down || key_up) && key == input::key::shift)
    {
        shift = key_down;
        return false;
    }

    const auto action = event.action && event.action->phase == input::action_phase::performed
                            ? event.action->action
                            : 0;
    const auto matches = [&](std::uint32_t binding, input::key portable)
    {
        return (binding && binding == action) || (key_down && key == portable);
    };
    if (feedback.phase == drag_phase::idle && matches(actions.lift, input::key::space))
    {
        for (auto it = views.rbegin(); it != views.rend(); ++it)
        {
            const auto lease = it->lock();
            if (lease && lease->owner &&
                start(capture(*lease->owner, lease->owner->impl_->focus_target),
                      drag_operation::none, true))
            {
                return true;
            }
        }

        return false;
    }

    if (feedback.phase == drag_phase::idle || feedback.phase == drag_phase::armed)
    {
        return false;
    }

    if (matches(actions.cancel, input::key::escape))
    {
        cancel(drag_cancel_reason::explicit_cancel);
        return true;
    }

    if (feedback.phase == drag_phase::pending)
    {
        return event.kind == input_kind::key_down || event.kind == input_kind::action;
    }

    if (matches(actions.confirm, input::key::enter))
    {
        request();
        return true;
    }

    if ((actions.previous && action == actions.previous) ||
        (key_down && key == input::key::tab && shift))
    {
        next(true);
        return true;
    }

    if ((actions.next && action == actions.next) || (key_down && key == input::key::tab))
    {
        next(false);
        return true;
    }

    if (action && (action == actions.copy || action == actions.move))
    {
        preferred = action == actions.copy ? drag_operation::copy : drag_operation::move;
        if (feedback.keyboard)
        {
            next(false);
        }
        else
        {
            point(feedback.position);
        }

        return true;
    }

    // Do not let keyboard activation/navigation accidentally activate widgets during a drag.
    return event.kind == input_kind::key_down || event.kind == input_kind::key_up ||
           event.kind == input_kind::action;
}

/// @brief Arm only a source in the ancestry of the frontmost eligible hit.
void drag_drop_session::impl::arm(layout_point position)
{
    for (auto it = views.rbegin(); it != views.rend(); ++it)
    {
        const auto lease = it->lock();
        if (!lease || !lease->owner)
        {
            continue;
        }

        auto node = lease->owner->impl_->hit(position);
        if (!node.generation)
        {
            continue;
        }

        while (node.generation)
        {
            visit();
            const auto value = capture(*lease->owner, node);
            if (const auto* options = source_options(value); options && valid(value))
            {
                source = value;
                offered = *options;
                origin = position;
                feedback = {
                    .phase = drag_phase::armed, .position = origin, .source = node, .target = {}};
                return;
            }

            const auto* region = lease->owner->impl_->region(node);
            node = region ? region->parent : element_handle{};
        }

        break;
    }
}

/// @brief Validate and route one replayable packet through the window drag state machine.
bool drag_drop_session::impl::process(input_event event)
{
    if (++packets > 4096)
    {
        throw std::overflow_error("drag input packet limit exhausted");
    }

    if (event.kind == input_kind::window_blur || event.kind == input_kind::pointer_cancel)
    {
        shift = false;
        cancel(drag_cancel_reason::window_lost);
        return false;
    }

    if (feedback.phase != drag_phase::idle && !valid_source())
    {
        cancel(view(source) ? drag_cancel_reason::source_unavailable
                            : drag_cancel_reason::context_lost);
    }

    if (keyboard(event))
    {
        return true;
    }

    if (event.kind == input_kind::pointer_down)
    {
        if (feedback.phase != drag_phase::idle)
        {
            cancel(drag_cancel_reason::explicit_cancel);
        }

        arm(event.position);
        return false;
    }

    if (feedback.phase == drag_phase::armed)
    {
        if (event.kind == input_kind::pointer_up)
        {
            reset();
            return false;
        }

        if (event.kind == input_kind::pointer_move &&
            std::hypot(double(event.position.x) - origin.x, double(event.position.y) - origin.y) >=
                offered.threshold)
        {
            feedback.position = event.position;
            if (start(source, drag_operation::none, false))
            {
                point(event.position);
                return true;
            }
        }

        return false;
    }

    if (feedback.phase == drag_phase::dragging)
    {
        if (event.kind == input_kind::pointer_leave)
        {
            select({}, drag_operation::none);
            return true;
        }

        if (event.kind == input_kind::pointer_move || event.kind == input_kind::pointer_up)
        {
            if (!feedback.keyboard)
            {
                point(event.position);
            }

            if (event.kind == input_kind::pointer_up)
            {
                request();
            }

            return true;
        }
    }

    return feedback.phase == drag_phase::pending;
}
} // namespace atlas::ui
