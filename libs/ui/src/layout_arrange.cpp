/** @file @brief Top-down slot placement with checked finite output and stable child ordering. */
#include "layout_flow.hpp"
#include "layout_numeric.hpp"
#include "layout_state.hpp"
#include <algorithm>

namespace atlas::ui::detail
{
namespace
{
/// @brief One axis is passed as an aggregate to keep origin/extent/bounds roles explicit.
struct axis_input
{
    double origin;              ///< Parent slot origin.
    double extent;              ///< Parent slot extent.
    float before;               ///< Leading outer margin.
    float after;                ///< Trailing outer margin.
    float desired;              ///< Measured border extent.
    float minimum;              ///< Own minimum, which may overflow the slot.
    float maximum;              ///< Own maximum, possibly unbounded.
    bool fixed;                 ///< Explicit requests do not stretch.
    layout_alignment alignment; ///< Position/automatic sizing policy.
};

/// @brief Return position and size, preserving minimum-overflow semantics.
std::pair<float, float> axis(axis_input input)
{
    const auto available = std::max(0., input.extent - input.before - input.after);
    const auto natural = input.alignment == layout_alignment::stretch && !input.fixed
                             ? available
                             : std::min(available, static_cast<double>(input.desired));
    const auto size =
        std::clamp(natural, static_cast<double>(input.minimum), static_cast<double>(input.maximum));
    double position = input.origin + input.before;
    if (input.alignment == layout_alignment::center)
    {
        position += (available - size) / 2;
    }

    if (input.alignment == layout_alignment::end)
    {
        position += available - size;
    }

    (void)layout_float(position + size);
    return {layout_float(position), layout_float(size)};
}

/// @brief Margins and own constraints apply uniformly to roots, overlays and stack slots.
layout_rect place(const layout_node& node, layout_extent desired, slot_rect slot)
{
    const auto& style = node.style;
    const auto [x, width] =
        axis({slot.x, slot.width, style.margin.left, style.margin.right, desired.width,
              style.constraints.minimum.width, style.constraints.maximum.width, node.width_fixed,
              style.horizontal});
    const auto [y, height] =
        axis({slot.y, slot.height, style.margin.top, style.margin.bottom, desired.height,
              style.constraints.minimum.height, style.constraints.maximum.height, node.height_fixed,
              style.vertical});
    return {x, y, width, height};
}
} // namespace

/// @brief Preorder guarantees that a child's slot is written before its descendants are placed.
arrange_output arrange_layout(const arrange_input& input, const std::stop_token& stop)
{
    auto result = std::make_shared<arranged_branch>(input.measured);
    const auto& nodes = input.measured->source->nodes;
    for (std::size_t index = 0; index < nodes.size(); ++index)
    {
        if (stop.stop_requested())
        {
            throw jobs::job_cancelled{};
        }

        result->items[index] = {nodes[index].target, {input.measured->desired[index], {}}};
    }

    if (nodes.empty())
    {
        return result;
    }

    result->items[0].geometry.bounds =
        place(nodes[0], input.measured->desired[0],
              {input.viewport.x, input.viewport.y, input.viewport.width, input.viewport.height});
    for (std::size_t index = 0; index < nodes.size(); ++index)
    {
        if (stop.stop_requested())
        {
            throw jobs::job_cancelled{};
        }

        const auto& value = nodes[index];
        const auto border = result->items[index].geometry.bounds;
        const auto padding = value.style.padding;
        const slot_rect content{
            static_cast<double>(border.x) + padding.left,
            static_cast<double>(border.y) + padding.top,
            std::max(0., static_cast<double>(border.width) - padding.left - padding.right),
            std::max(0., static_cast<double>(border.height) - padding.top - padding.bottom)};
        const auto flow = describe_flow(value.panel);
        const bool canvas = std::holds_alternative<canvas_layout>(value.panel);
        double cursor = 0;
        for (auto child = value.first; child != no_layout_node; child = nodes[child].next)
        {
            if (stop.stop_requested())
            {
                throw jobs::job_cancelled{};
            }

            auto slot = content;
            const auto desired = input.measured->desired[child];
            const auto margin = nodes[child].style.margin;
            if (flow.horizontal)
            {
                slot.x += cursor;
                slot.width = static_cast<double>(desired.width) + margin.left + margin.right;
                cursor += slot.width + flow.spacing;
            }

            if (flow.vertical)
            {
                slot.y += cursor;
                slot.height = static_cast<double>(desired.height) + margin.top + margin.bottom;
                cursor += slot.height + flow.spacing;
            }

            result->items[child].geometry.bounds =
                canvas ? place_canvas(nodes[child], desired, content,
                                      placement(*input.measured->source, nodes[child]))
                       : place(nodes[child], desired, slot);
        }
    }

    return result;
}

/// @brief Range cells and their newly allocated branch arrays have exclusive worker ownership.
void arrange_roots(const phase_range<arrange_input, arrange_output>& range)
{
    for (std::size_t index = 0; index < range.inputs.size(); ++index)
    {
        range.throw_if_cancelled();
        range.outputs[index] = arrange_layout(range.inputs[index], range.cancellation);
    }
}
} // namespace atlas::ui::detail
