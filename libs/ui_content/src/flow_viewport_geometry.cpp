/** @file @brief Mapped submenu layout size is independent from inherited host clipping. */
#include "flow_viewport_state.hpp"
#include <algorithm>
#include <atlas/ui/root_presentation.hpp>

namespace atlas::ui_content
{
namespace
{
/// @brief Map inherited translations without silently reflowing scaled or rotated hosts.
ui::layout_rect translated(instance& view, const ui::layout_snapshot& geometry,
                           ui::element_handle node)
{
    const auto item = geometry.find(node);
    if (!item)
    {
        throw std::invalid_argument("Flow host has no layout geometry");
    }

    auto result = item->bounds;
    for (auto ancestor = node; ancestor.generation; ancestor = view.tree().parent(ancestor))
    {
        const auto style = view.tree().paint(ancestor);
        if (style.scale != ui::layout_point{1, 1} || style.rotation != 0)
        {
            throw std::invalid_argument(
                "Flow submenu host scale/rotation requires a transformed surface adapter");
        }

        result.x += style.translation.x;
        result.y += style.translation.y;
    }

    return result;
}
} // namespace

/// @brief Arrange at the full host size; clip paint/input without shrinking the child's layout.
void flow_viewport::impl::arrange(ui::layout_rect bounds)
{
    const auto shown = flow.presentation(); // Checks affinity before touching coordinator state.
    layers.clear();
    for (auto item : shown)
    {
        auto area = bounds;
        auto clip = bounds;
        float opacity = 1;
        if (item.parent && !item.host.empty())
        {
            const auto parent = std::ranges::find_if(layers,
                                                     [&](const auto& value)
                                                     {
                                                         return value.item.view == item.parent;
                                                     });
            if (parent == layers.end())
            {
                throw std::logic_error("Flow parent must precede its submenu");
            }

            const auto host = item.parent->find(item.host);
            area = translated(*item.parent, parent->geometry, host);
            clip = ui::intersect_clip(parent->bounds, area);
            opacity = parent->opacity;
            for (auto node = host; node.generation; node = item.parent->tree().parent(node))
            {
                const auto requested = item.parent->tree().requested(node);
                if (!requested.visible)
                {
                    clip.width = 0;
                }

                opacity *= requested.opacity;
                if (item.parent->tree().paint(node).clip)
                {
                    clip =
                        ui::intersect_clip(clip, translated(*item.parent, parent->geometry, node));
                }
            }
        }

        auto geometry = layout.update(item.view->tree(), area);
        auto source = item.view->tree().snapshot();
        auto hit = clip;
        if (item.popup)
        {
            const auto nodes = source.elements();
            if (!nodes.empty())
            {
                hit = ui::intersect_clip(
                    clip, ui::root_visual_bounds(source, geometry, nodes.front().target));
            }
        }

        item.input = item.input && clip.width > 0 && clip.height > 0 && !source.elements().empty();
        layers.push_back({item, std::move(source), std::move(geometry), hit, opacity});
    }
}
} // namespace atlas::ui_content
