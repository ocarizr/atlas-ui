/** @file @brief Wheel propagation and captured proportional thumb dragging on the input
 * scroll_owner. */
#include "input_state.hpp"
#include "snapshot_lookup.hpp"
#include <cmath>

namespace atlas::ui
{
namespace
{
/// @brief Clamp in double precision so extreme finite wheel deltas do not overflow floats.
float advance(float position, double wheel_delta, float range)
{
    return static_cast<float>(std::clamp(position + wheel_delta, 0., static_cast<double>(range)));
}

/// @brief Select a coordinate only after the public axis has been validated.
float coordinate(layout_point point, scroll_axis axis)
{
    return axis == scroll_axis::horizontal ? point.x : point.y;
}

/// @brief Read a finite main-axis extent or origin without hidden SDK conversions.
float length(layout_rect rect, scroll_axis axis)
{
    return axis == scroll_axis::horizontal ? rect.width : rect.height;
}
} // namespace

/// @brief Size/range changes or loss of the controlled scroll_owner cancel a captured drag.
bool input_session::impl::scroll_capture_valid() const
{
    if (!dragging)
    {
        return true;
    }

    if (!scrolling)
    {
        return false;
    }

    const auto bar = scrolling->geometry.find_scrollbar(dragging->target);
    if (!bar || !bar->enabled || !eligible(bar->viewport))
    {
        return false;
    }

    const auto scroll_owner = scrolling->geometry.find_scroll(bar->viewport);
    const auto* current = region(dragging->target);
    return scroll_owner && current && current->transform == dragging->transform &&
           bar->viewport == dragging->geometry.viewport && bar->axis == dragging->geometry.axis &&
           bar->track == dragging->geometry.track &&
           bar->thumb.width == dragging->geometry.thumb.width &&
           bar->thumb.height == dragging->geometry.thumb.height &&
           coordinate(scroll_owner->range, bar->axis) == dragging->range;
}

/// @brief Consume a wheel packet only if this viewport can move; otherwise allow ancestor bubbling.
bool input_session::impl::scroll_wheel(element_handle current, input_event raw)
{
    const auto chrome = scrolling->geometry.find_scrollbar(current);
    if (chrome)
    {
        current = chrome->viewport;
    }

    if (!eligible(current))
    {
        return false;
    }

    auto* pending = scroll(current);
    const auto* node = detail::find_element(scrolling->source, current);
    const auto* policy = node ? std::get_if<scroll_layout>(&node->panel) : nullptr;
    if (!pending || !policy)
    {
        return false;
    }

    auto wheel_delta = raw.scroll;
    if (policy->horizontal && !policy->vertical && wheel_delta.x == 0)
    {
        wheel_delta.x = wheel_delta.y;
        wheel_delta.y = 0;
    }

    const diagnostics::operation_scope scope{destinations,
                                             {"ui.input.scroll.wheel", "ui.dispatch"}};
    const auto before = pending->offset;
    queue_scroll(
        *pending,
        {advance(before.x, static_cast<double>(wheel_delta.x) * 40, pending->geometry.range.x),
         advance(before.y, static_cast<double>(wheel_delta.y) * 40, pending->geometry.range.y)});
    return before != pending->offset;
}

/// @brief Default handlers run after user target/bubble callbacks and honor an earlier stop
/// response.
bool input_session::impl::scroll_response(element_handle current, event_kind kind, input_event raw)
{
    if (!scrolling || (scrolling->pending.empty() && scrolling->geometry.scrollbars().empty()) ||
        !eligible(current))
    {
        return false;
    }

    if (kind == event_kind::pointer_scroll)
    {
        return scroll_wheel(current, raw);
    }

    if (kind == event_kind::key_down || kind == event_kind::action)
    {
        return !focus_navigation_packet(raw, false) && scroll_navigation(current, raw);
    }

    const auto bar = scrolling->geometry.find_scrollbar(current);
    if (!bar || !bar->enabled || !eligible(bar->viewport))
    {
        return false;
    }

    const diagnostics::operation_scope scope{destinations,
                                             {"ui.input.scroll.pointer", "ui.dispatch"}};
    auto* pending = scroll(bar->viewport);
    if (!pending)
    {
        return false;
    }

    const auto* value = region(current);
    const auto local = value->transform.local(value->border, raw.position);
    const layout_point point{value->border.x + local.x, value->border.y + local.y};
    const auto position = coordinate(point, bar->axis);
    const auto range = coordinate(pending->geometry.range, bar->axis);
    const auto travel = length(bar->track, bar->axis) - length(bar->thumb, bar->axis);
    auto offset = coordinate(pending->offset, bar->axis);
    if (kind == event_kind::pointer_down)
    {
        const auto start = coordinate({bar->thumb.x, bar->thumb.y}, bar->axis);
        if (position >= start && position < start + length(bar->thumb, bar->axis))
        {
            clear_capture();
            capture_target = current;
            dragging = scroll_drag{current, *bar, position, offset, value->transform, range};
            return true;
        }

        const auto page = bar->axis == scroll_axis::horizontal ? pending->geometry.viewport.width
                                                               : pending->geometry.viewport.height;
        offset = advance(offset, position < start ? -static_cast<double>(page) : page, range);
    }
    else if ((kind == event_kind::pointer_move || kind == event_kind::pointer_up) && dragging &&
             dragging->target == current)
    {
        offset = advance(
            dragging->offset,
            (static_cast<double>(position) - dragging->press) * dragging->range / travel, range);
    }
    else
    {
        return false;
    }

    auto next = pending->offset;
    if (bar->axis == scroll_axis::horizontal)
    {
        next.x = offset;
    }
    else
    {
        next.y = offset;
    }

    queue_scroll(*pending, next);
    return true;
}
} // namespace atlas::ui
