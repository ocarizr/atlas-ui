/**
 * @file
 * @brief General transform-origin metadata, independent from canvas placement pivots.
 */
#pragma once
#include <atlas/ui/layout_types.hpp>

namespace atlas::ui
{
struct layout_point
{
    float x = 0; ///< Horizontal logical or normalized coordinate, according to the contract.
    float y = 0; ///< Vertical logical or normalized coordinate.
    bool operator==(const layout_point&) const = default; ///< Compare coordinates.
}; ///< Lightweight value without native SDK types.

struct visual_style
{
    layout_point origin{0.5f, 0.5f}; ///< Normalized border-relative origin in [0, 1] on each axis.
    bool operator==(const visual_style&) const = default; ///< Exact visual equality.
}; ///< Metadata for any element; no rotation or scale is applied by layout.

layout_point
resolve_transform_origin(layout_rect bounds,
                         visual_style visual = {}); ///< Resolve an untransformed border pivot.
} // namespace atlas::ui
