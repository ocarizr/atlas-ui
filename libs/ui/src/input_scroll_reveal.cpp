/** @file @brief Minimal deterministic nested reveal, independent of Lua and navigation device. */
#include "input_state.hpp"
#include "snapshot_lookup.hpp"
#include <cmath>

namespace atlas::ui
{
namespace
{
/// @brief Explicit roles avoid ambiguous adjacent coordinate arguments.
struct reveal_axis
{
    float start, size, view_start, view_size, offset; ///< Item, viewport and current offset.
    bool first, last; ///< Boundary roles in the enabled focusable content sequence.
};

/// @brief Oversized and single items have a stable leading-edge policy; ordinary items move
/// minimally.
float reveal_position(reveal_axis value, float range)
{
    if (value.first)
    {
        return 0;
    }

    if (value.last && value.size <= value.view_size)
    {
        return range;
    }

    const double before = static_cast<double>(value.start) - value.view_start;
    const double after = before + value.size - value.view_size;
    const auto change = value.size > value.view_size || before < 0 ? before : std::max(0., after);
    return static_cast<float>(
        std::clamp(static_cast<double>(value.offset) + change, 0., static_cast<double>(range)));
}

/// @brief Scroll masks preserve viewport axes, so opposite hull corners suffice for inverse bounds.
layout_rect local_rect(const detail::presentation_region& scroll_owner, layout_rect bounds)
{
    const auto first = scroll_owner.transform.local(scroll_owner.border, {bounds.x, bounds.y});
    const auto last = scroll_owner.transform.local(
        scroll_owner.border, {bounds.x + bounds.width, bounds.y + bounds.height});
    return {scroll_owner.border.x + std::min(first.x, last.x),
            scroll_owner.border.y + std::min(first.y, last.y), std::abs(last.x - first.x),
            std::abs(last.y - first.y)};
}
} // namespace

/// @brief Stable layout coordinates, not insertion order or worker completion, define boundaries.
std::array<bool, 4> input_session::impl::scroll_edges(element_handle scroll_owner,
                                                      element_handle target) const
{
    const auto* binding = find(target);
    const auto geometry = scrolling->geometry.find(target);
    if (!binding || !binding->options.focusable || !geometry)
    {
        return {};
    }

    const auto bounds = geometry->bounds;
    std::array<bool, 4> edges{true, true, true, true};
    for (const auto& candidate : bindings)
    {
        if (!candidate.options.focusable || !eligible(candidate.target) ||
            candidate.target == target ||
            !detail::is_descendant(scrolling->source, candidate.target, scroll_owner,
                                   limits.max_route_depth))
        {
            continue;
        }

        if (const auto other = scrolling->geometry.find(candidate.target))
        {
            edges[0] = edges[0] && bounds.x <= other->bounds.x;
            edges[1] = edges[1] && bounds.x + bounds.width >= other->bounds.x + other->bounds.width;
            edges[2] = edges[2] && bounds.y <= other->bounds.y;
            edges[3] =
                edges[3] && bounds.y + bounds.height >= other->bounds.y + other->bounds.height;
        }
    }

    return edges;
}

/// @brief Inner reveal displacement is accounted for before calculating an outer reveal.
void input_session::impl::reveal(element_handle target)
{
    const auto* target_region = region(target);
    if (!target_region || !eligible(target))
    {
        return;
    }

    const diagnostics::operation_scope scope{destinations,
                                             {"ui.input.scroll.reveal", "ui.dispatch"}};
    auto bounds = target_region->bounds;
    for (auto parent = target_region->parent; parent.generation && eligible(target);
         parent = region(parent)->parent)
    {
        if (send(parent, event_kind::ensure_visible, route_phase::bubble, target).stop)
        {
            break;
        }

        auto* pending = scroll(parent);
        if (!pending || !eligible(parent))
        {
            continue;
        }

        const auto* scroll_owner = region(parent);
        if (!scroll_owner || !scroll_owner->transform.invertible())
        {
            continue;
        }

        auto item = local_rect(*scroll_owner, bounds);
        item.x -= pending->offset.x - pending->geometry.offset.x;
        item.y -= pending->offset.y - pending->geometry.offset.y;
        const auto view = pending->geometry.viewport;
        const auto edges = scroll_edges(parent, target);
        const auto previous = pending->offset;
        queue_scroll(*pending, {reveal_position({item.x, item.width, view.x, view.width, previous.x,
                                                 edges[0], edges[1]},
                                                pending->geometry.range.x),
                                reveal_position({item.y, item.height, view.y, view.height,
                                                 previous.y, edges[2], edges[3]},
                                                pending->geometry.range.y)});
        const auto dx = pending->offset.x - pending->geometry.offset.x;
        const auto dy = pending->offset.y - pending->geometry.offset.y;
        bounds.x -=
            static_cast<float>(scroll_owner->transform.xx * dx + scroll_owner->transform.xy * dy);
        bounds.y -=
            static_cast<float>(scroll_owner->transform.yx * dx + scroll_owner->transform.yy * dy);
    }
}

/// @brief Explicit reveal does not change focus or replay it continuously on later frames.
void input_session::ensure_visible(element_handle target)
{
    impl_->open();
    if (!impl_->eligible(target))
    {
        throw std::invalid_argument("reveal needs a live enabled UI target");
    }

    const impl::guard guard{impl_->busy};
    try
    {
        impl_->reveal(target);
    }
    catch (...)
    {
        impl_->fail();
        throw;
    }
}
} // namespace atlas::ui
