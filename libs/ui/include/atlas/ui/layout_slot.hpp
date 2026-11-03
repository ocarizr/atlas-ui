/**
 * @file
 * @brief Canvas-specific parent-owned child placement without a universal property bag.
 */
#pragma once
#include <atlas/ui/commands.hpp>

namespace atlas::ui
{
/// @brief Common fixed requests take precedence over a stretched canvas slot.
enum class canvas_sizing : std::uint8_t
{
    desired, ///< Use the child's measured border size, without parent-space capping.
    stretch  ///< Fill the anchor interval unless the child axis is explicitly fixed.
};

/// @brief Positions are relative to parent content; offsets are signed logical units.
struct canvas_axis
{
    float anchor_min = 0; ///< Normalized leading anchor in [0, 1].
    float anchor_max = 0; ///< Normalized trailing anchor, at least anchor_min.
    float offset_min = 0; ///< Add to the leading anchor position.
    float offset_max = 0; ///< Subtract from the trailing anchor position.
    float pivot = 0; ///< Normalized alignment of the border within margin-reduced anchor space.
    canvas_sizing sizing = canvas_sizing::desired;       ///< Independent per-axis sizing policy.
    bool operator==(const canvas_axis&) const = default; ///< Exact placement identity.
};

struct canvas_slot
{
    canvas_axis horizontal{}; ///< Defaults to desired size at the content's left edge.
    canvas_axis vertical{};   ///< Defaults to desired size at the content's top edge.
    bool operator==(const canvas_slot&) const = default; ///< Compare both axes.
}; ///< Placement belongs to the parent-child relationship, not the child's panel type.

struct canvas_slot_record
{
    element_handle target; ///< Exact child identity at capture.
    canvas_slot slot;      ///< Explicit override; default slots need no sparse record.
}; ///< Immutable snapshot entry in ascending live-slot order.
} // namespace atlas::ui
