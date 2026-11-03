/** @file @brief Canvas axes share one checked formula for absolute measure and final arrangement.
 */
#include "layout_numeric.hpp"
#include "layout_state.hpp"
#include <algorithm>

namespace atlas::ui::detail
{
namespace
{
struct canvas_axis_input
{
    double origin;    ///< Resolved parent content origin.
    double extent;    ///< Resolved parent content extent.
    float before;     ///< Leading outer margin.
    float after;      ///< Trailing outer margin.
    float desired;    ///< Measured border extent.
    float minimum;    ///< Border minimum may overflow available space.
    float maximum;    ///< Border maximum may be unbounded.
    bool fixed;       ///< Explicit child requests override slot stretch.
    canvas_axis slot; ///< Typed parent-owned axis policy.
}; ///< Avoid ambiguous positional numeric parameters.

/// @brief Desired mode never silently shrinks content to an anchor point's zero-sized interval.
std::pair<float, float> place_axis(const canvas_axis_input& input)
{
    const auto& slot = input.slot;
    const double start = input.origin + input.extent * slot.anchor_min + slot.offset_min;
    const double end = input.origin + input.extent * slot.anchor_max - slot.offset_max;
    const double available = std::max(0., end - start - input.before - input.after);
    const double natural =
        slot.sizing == canvas_sizing::stretch && !input.fixed ? available : input.desired;
    const double size =
        std::clamp(natural, static_cast<double>(input.minimum), static_cast<double>(input.maximum));
    const double position = start + input.before + (available - size) * slot.pivot;
    (void)layout_float(position + size);
    return {layout_float(position), layout_float(size)};
}
} // namespace

/// @brief Common min/max, requested sizing and margins remain active inside canvas slots.
layout_rect place_canvas(const layout_node& node, layout_extent desired, slot_rect content,
                         const canvas_slot& slot, canvas_axes axes)
{
    const auto& style = node.style;
    const auto [x, width] =
        axes.horizontal
            ? place_axis({content.x, content.width, style.margin.left, style.margin.right,
                          desired.width, style.constraints.minimum.width,
                          style.constraints.maximum.width, node.width_fixed, slot.horizontal})
            : std::pair{0.f, 0.f};
    const auto [y, height] =
        axes.vertical
            ? place_axis({content.y, content.height, style.margin.top, style.margin.bottom,
                          desired.height, style.constraints.minimum.height,
                          style.constraints.maximum.height, node.height_fixed, slot.vertical})
            : std::pair{0.f, 0.f};
    return {x, y, width, height};
}

/// @brief Validation ensures these slots contain no relative-anchor or stretch equations.
layout_extent measure_canvas_child(const layout_node& node, layout_extent desired,
                                   const canvas_slot& slot)
{
    const auto bounds = place_canvas(node, desired, {0, 0, 0, 0}, slot);
    return {layout_float(std::max(0., static_cast<double>(bounds.x) + bounds.width +
                                          node.style.margin.right)),
            layout_float(std::max(0., static_cast<double>(bounds.y) + bounds.height +
                                          node.style.margin.bottom))};
}
} // namespace atlas::ui::detail
