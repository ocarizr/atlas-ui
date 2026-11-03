/** @file @brief Logical intermediate slots and isolated canvas numeric policy. */
#pragma once
#include <atlas/ui/layout_slot.hpp>
#include <atlas/ui/layout_types.hpp>

namespace atlas::ui::detail
{
struct layout_node;     ///< Immutable common inputs defined by layout preparation.
struct arranged_branch; ///< Private output whose sibling sources are already arranged.

struct slot_rect
{
    double x;      ///< Intermediate leading position.
    double y;      ///< Intermediate top position.
    double width;  ///< Intermediate available width.
    double height; ///< Intermediate available height.
}; ///< Double accumulation precedes checked float publication.

struct canvas_axes
{
    bool horizontal = true; ///< Resolve the ordinary horizontal canvas policy.
    bool vertical = true;   ///< Resolve the ordinary vertical canvas policy.
}; ///< Replaced axes are not evaluated as unused intermediate rectangles.

layout_rect place_canvas(const layout_node& node, layout_extent desired, slot_rect content,
                         const canvas_slot& slot, canvas_axes axes = {}); ///< Canvas placement.
layout_extent
measure_canvas_child(const layout_node& node, layout_extent desired,
                     const canvas_slot& slot); ///< Absolute child contribution at origin zero.
/// @brief Resolve ordinary and related canvas axes without unused intermediate rectangles.
void place_boundaries(arranged_branch& result, std::uint32_t child, slot_rect content);
} // namespace atlas::ui::detail
