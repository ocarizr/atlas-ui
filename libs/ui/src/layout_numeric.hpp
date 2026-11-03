/** @file @brief Logical intermediate slots and isolated canvas numeric policy. */
#pragma once
#include <atlas/ui/layout_slot.hpp>
#include <atlas/ui/layout_types.hpp>

namespace atlas::ui::detail
{
struct layout_node; ///< Immutable common inputs defined by layout preparation.

struct slot_rect
{
    double x;      ///< Intermediate leading position.
    double y;      ///< Intermediate top position.
    double width;  ///< Intermediate available width.
    double height; ///< Intermediate available height.
}; ///< Double accumulation precedes checked float publication.

layout_rect place_canvas(const layout_node& node, layout_extent desired, slot_rect content,
                         const canvas_slot& slot); ///< Canvas-specific child border placement.
layout_extent
measure_canvas_child(const layout_node& node, layout_extent desired,
                     const canvas_slot& slot); ///< Absolute child contribution at origin zero.
} // namespace atlas::ui::detail
