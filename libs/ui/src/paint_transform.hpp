/** @file @brief Shared presentation coordinates for painting, masks and input. */
#pragma once
#include <atlas/ui/element_state.hpp>

namespace atlas::ui::detail
{
/// @brief Affine map from untransformed layout coordinates to logical viewport coordinates.
struct paint_transform
{
    double xx = 1; ///< Horizontal contribution to viewport x.
    double xy = 0; ///< Vertical contribution to viewport x.
    double yx = 0; ///< Horizontal contribution to viewport y.
    double yy = 1; ///< Vertical contribution to viewport y.
    double x = 0;  ///< Viewport x offset.
    double y = 0;  ///< Viewport y offset.

    [[nodiscard]] bool
    axes_preserved() const noexcept; ///< Can an exact rectangular scissor represent the result?
    [[nodiscard]] bool translation_only() const noexcept; ///< Unit linear map.
    [[nodiscard]] layout_point
    project(layout_point point) const; ///< Map a point with finite output checks.
    [[nodiscard]] layout_rect
    bounds(layout_rect border) const; ///< Axis-aligned hull of the transformed border.
    [[nodiscard]] layout_point local(layout_rect border,
                                     layout_point point) const; ///< Inverse viewport mapping.
    [[nodiscard]] bool hit(layout_rect border,
                           layout_point point) const noexcept; ///< Half-open inverse test.
    [[nodiscard]] bool invertible() const noexcept; ///< Collapsed shapes cannot be hit or painted.
};

/// @brief Apply local scale, rotation and translation before the inherited map.
[[nodiscard]] paint_transform compose(const paint_transform& parent, layout_rect border,
                                      const element_record& node);
} // namespace atlas::ui::detail
