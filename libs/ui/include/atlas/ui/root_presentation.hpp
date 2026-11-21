/**
 * @file
 * @brief Root visual bounds and hit tests reuse the same affine rules as paint and input.
 */
#pragma once
#include <atlas/ui/element_snapshot.hpp>
#include <atlas/ui/layout_snapshot.hpp>

namespace atlas::ui
{
/// @brief Resolve one forest root's transformed border from matching immutable snapshots.
[[nodiscard]] layout_rect root_visual_bounds(const element_snapshot& source,
                                             const layout_snapshot& geometry, element_handle root);
/// @brief Inverse-hit the same root border; collapsed transforms never contain a point.
[[nodiscard]] bool root_visual_contains(const element_snapshot& source,
                                        const layout_snapshot& geometry, element_handle root,
                                        layout_point point);
} // namespace atlas::ui
