/** @file @brief Deterministic half-open hit tests, state transitions and hierarchy routes. */
#include "input_state.hpp"
#include <cmath>

namespace atlas::ui
{
namespace
{
/// @brief Shared edges belong to only one adjacent rectangle.
bool contains(layout_rect bounds, layout_point point)
{
    return point.x >= bounds.x && point.y >= bounds.y &&
           static_cast<double>(point.x) < static_cast<double>(bounds.x) + bounds.width &&
           static_cast<double>(point.y) < static_cast<double>(bounds.y) + bounds.height;
}
} // namespace

/// @brief Reverse isolated painter order includes unpainted registered borders.
element_handle input_session::impl::hit(layout_point point) const
{
    for (auto it = regions.rbegin(); it != regions.rend(); ++it)
    {
        const auto* binding = find(it->target);
        if (binding && binding->options.hit_test && contains(it->bounds, point) &&
            contains(it->clip, point) && eligible(it->target))
        {
            return it->target;
        }
    }

    return {};
}

/// @brief Reject invalid points before observing presentation or live state.
element_handle input_session::hit_test(layout_point point) const
{
    impl_->check();
    if (!std::isfinite(point.x) || !std::isfinite(point.y))
    {
        throw std::invalid_argument("invalid UI pointer coordinates");
    }

    return impl_->hit(point);
}

/// @brief Focus reveal bubbles even for an eligible target outside its rectangular clip.
void input_session::impl::set_focus(element_handle target)
{
    if (focus_target == target)
    {
        return;
    }

    const auto previous = std::exchange(focus_target, {});
    if (previous.generation != 0)
    {
        (void)send(previous, event_kind::focus_lost, route_phase::direct, previous);
    }

    if (target.generation == 0 || !eligible(target))
    {
        return;
    }

    focus_target = target;
    (void)send(target, event_kind::focus_gained, route_phase::direct, target);
    auto parent = region(target)->parent;
    while (parent.generation != 0 && eligible(target))
    {
        if (send(parent, event_kind::ensure_visible, route_phase::bubble, target).stop)
        {
            break;
        }

        parent = region(parent)->parent;
    }
}

/// @brief Explicit focus is not keyboard/gamepad navigation policy.
void input_session::focus(element_handle target)
{
    impl_->open();
    if (target.generation != 0)
    {
        const auto* binding = impl_->find(target);
        if (!binding || !binding->options.focusable || !impl_->eligible(target))
        {
            throw std::invalid_argument("UI focus needs a live enabled focusable target");
        }
    }

    const impl::guard guard{impl_->busy};
    try
    {
        impl_->set_focus(target);
        impl_->reconcile();
    }
    catch (...)
    {
        impl_->fail();
        throw;
    }
}

/// @brief Validate the complete response before any of its ownership changes.
void input_session::impl::respond(input_binding& binding, const event_response& response,
                                  input_event raw)
{
    if ((response.capture_pointer && response.release_pointer) ||
        (response.focus && response.blur) ||
        (response.capture_pointer && raw.kind != input_kind::pointer_down) ||
        (response.release_pointer && raw.kind != input_kind::pointer_down &&
         raw.kind != input_kind::pointer_move && raw.kind != input_kind::pointer_up &&
         raw.kind != input_kind::pointer_cancel) ||
        (response.focus && !binding.options.focusable))
    {
        throw std::invalid_argument("invalid UI input response");
    }

    if (!eligible(binding.target))
    {
        return;
    }

    if (response.release_pointer && capture_target == binding.target)
    {
        clear_capture();
    }

    if (response.capture_pointer && capture_target != binding.target)
    {
        clear_capture();
        if (eligible(binding.target))
        {
            capture_target = binding.target;
        }
    }

    if (response.focus)
    {
        set_focus(binding.target);
    }

    if (response.blur)
    {
        set_focus({});
    }
}

/// @brief A fixed identity path cannot be invalidated by registry removal inside a callback.
void input_session::impl::route(input_event raw, element_handle target, event_kind kind)
{
    if (!eligible(target))
    {
        return;
    }

    path.clear();
    for (auto current = target; current.generation != 0; current = region(current)->parent)
    {
        path.push_back(current);
    }

    const auto deliver = [&](element_handle current, route_phase phase)
    {
        if (!eligible(target))
        {
            return true;
        }

        auto* binding = find(current);
        if (!binding || !binding->enabled || !eligible(current))
        {
            return false;
        }

        const auto response = send(current, kind, phase, target, raw);
        respond(*binding, response, raw);
        return response.stop;
    };
    for (std::size_t i = path.size(); i-- > 1;)
    {
        if (deliver(path[i], route_phase::capture))
        {
            return;
        }
    }

    if (deliver(target, route_phase::target))
    {
        return;
    }

    for (std::size_t i = 1; i < path.size(); ++i)
    {
        if (deliver(path[i], route_phase::bubble))
        {
            return;
        }
    }
}

/// @brief Packet order, not timestamps or worker completion, defines replay semantics.
void input_session::impl::process_one(input_event raw)
{
    switch (raw.kind)
    {
        case input_kind::pointer_move:
        case input_kind::pointer_down:
        case input_kind::pointer_up:
        {
            const auto under = hit(raw.position);
            set_hover(under, raw);
            const auto target = capture_target.generation != 0 ? capture_target : under;
            const auto kind = raw.kind == input_kind::pointer_move   ? event_kind::pointer_move
                              : raw.kind == input_kind::pointer_down ? event_kind::pointer_down
                                                                     : event_kind::pointer_up;
            route(raw, target, kind);
            if (raw.kind == input_kind::pointer_up)
            {
                clear_capture();
            }

            break;
        }
        case input_kind::pointer_cancel:
            route(raw, capture_target, event_kind::pointer_cancel);
            clear_capture();
            break;
        case input_kind::pointer_leave:
            set_hover({}, raw);
            break;
        case input_kind::key_down:
        case input_kind::key_up:
            route(raw, focus_target,
                  raw.kind == input_kind::key_down ? event_kind::key_down : event_kind::key_up);
            break;
        case input_kind::window_blur:
            route(raw, capture_target, event_kind::pointer_cancel);
            clear_capture();
            set_focus({});
            set_hover({}, raw);
            break;
    }

    reconcile();
}
} // namespace atlas::ui
