/** @file @brief Preserve a retained focused anchor on layout changes without undoing manual scroll.
 */
#include "input_state.hpp"
#include "snapshot_lookup.hpp"

namespace atlas::ui
{
/// @brief Only actual content-position changes compensate offsets; unchanged focus never snaps
/// back.
void input_session::impl::preserve_scroll_anchor(const scroll_input_frame& previous)
{
    if (!scrolling || !focus_target.generation || previous.focus != focus_target ||
        !eligible(focus_target))
    {
        return;
    }

    const auto before = previous.geometry.find(focus_target);
    const auto after = scrolling->geometry.find(focus_target);
    if (!before || !after)
    {
        return;
    }

    layout_point compensation{};
    const auto* node = detail::find_element(scrolling->source, focus_target);
    for (auto current = node ? node->parent : element_handle{}; current.generation;)
    {
        node = detail::find_element(scrolling->source, current);
        if (!node)
        {
            break;
        }

        const auto ancestor = current;
        current = node->parent;
        auto* value = scroll(ancestor);
        if (!value)
        {
            continue;
        }

        const auto* owner_binding = find(ancestor);
        if (owner_binding && owner_binding->logical_anchor && !owner_binding->retiring)
        {
            continue;
        }

        auto& pending = *value;
        const auto old = previous.geometry.find_scroll(pending.target);
        if (!old ||
            !detail::is_descendant(previous.source, focus_target, pending.target,
                                   limits.max_route_depth) ||
            !detail::is_descendant(scrolling->source, focus_target, pending.target,
                                   limits.max_route_depth))
        {
            continue;
        }

        const double dx = (static_cast<double>(after->bounds.x) - pending.geometry.viewport.x) -
                          (static_cast<double>(before->bounds.x) - old->viewport.x);
        const double dy = (static_cast<double>(after->bounds.y) - pending.geometry.viewport.y) -
                          (static_cast<double>(before->bounds.y) - old->viewport.y);
        const auto prior = pending.offset;
        queue_scroll(
            pending,
            {static_cast<float>(std::clamp(pending.offset.x + dx - compensation.x, 0.,
                                           static_cast<double>(pending.geometry.range.x))),
             static_cast<float>(std::clamp(pending.offset.y + dy - compensation.y, 0.,
                                           static_cast<double>(pending.geometry.range.y)))});
        compensation.x += pending.offset.x - prior.x;
        compensation.y += pending.offset.y - prior.y;
    }
}
} // namespace atlas::ui
