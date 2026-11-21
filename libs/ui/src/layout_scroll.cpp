/** @file @brief Sparse scroll presentation published atomically with unchanged numeric layout. */
#include "layout_state.hpp"
#include <algorithm>

namespace atlas::ui::detail
{
namespace
{
/// @brief Insets saturate at zero; no infinite available space enters layout arithmetic.
layout_rect content_bounds(layout_rect border, layout_insets padding)
{
    return {layout_float(static_cast<double>(border.x) + padding.left),
            layout_float(static_cast<double>(border.y) + padding.top),
            layout_float(
                std::max(0., static_cast<double>(border.width) - padding.left - padding.right)),
            layout_float(
                std::max(0., static_cast<double>(border.height) - padding.top - padding.bottom))};
}

/// @brief Child outer size includes explicitly authored margins, without paint transforms.
layout_extent content_extent(const element_snapshot& source, const layout_snapshot_data& output,
                             element_handle child)
{
    if (child == element_handle{})
    {
        return {};
    }

    const auto nodes = source.elements();
    const auto found = std::lower_bound(nodes.begin(), nodes.end(), child.index,
                                        [](const element_record& node, std::uint32_t index)
                                        {
                                            return node.target.index < index;
                                        });
    if (found == nodes.end() || found->target != child)
    {
        throw std::invalid_argument("invalid scroll content identity");
    }

    const auto index = static_cast<std::size_t>(found - nodes.begin());
    const auto border = output.items[index].geometry.bounds;
    const auto margin = found->layout.margin;
    return {layout_float(static_cast<double>(border.width) + margin.left + margin.right),
            layout_float(static_cast<double>(border.height) + margin.top + margin.bottom)};
}
} // namespace

/// @brief Offset is excluded from numeric cache keys; only this sparse output is rebuilt.
void prepare_scrolls(const element_snapshot& source, layout_snapshot_data& output)
{
    const auto nodes = source.elements();
    for (std::size_t index = 0; index < nodes.size(); ++index)
    {
        const auto& node = nodes[index];
        const auto* scroll = std::get_if<scroll_layout>(&node.panel);
        if (!scroll)
        {
            continue;
        }

        scroll_geometry result;
        result.viewport = content_bounds(output.items[index].geometry.bounds, node.layout.padding);
        result.extent = content_extent(source, output, node.first_child);
        result.range = {
            scroll->horizontal ? std::max(0.f, result.extent.width - result.viewport.width) : 0,
            scroll->vertical ? std::max(0.f, result.extent.height - result.viewport.height) : 0};
        result.offset = {std::clamp(scroll->offset.x, 0.f, result.range.x),
                         std::clamp(scroll->offset.y, 0.f, result.range.y)};
        output.scrolls.push_back({node.target, result});
    }
}
} // namespace atlas::ui::detail
