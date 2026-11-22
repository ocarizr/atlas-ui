/**
 * @file
 * @brief Portable scrollbar links and immutable track/thumb presentation geometry.
 */
#pragma once
#include <atlas/ui/layout_types.hpp>

namespace atlas::ui
{
/// @brief Axis addressed by an independent scrollbar element.
enum class scroll_axis : std::uint8_t
{
    horizontal, ///< Track advances left to right.
    vertical    ///< Track advances top to bottom.
};

/// @brief Sparse link from a chrome element outside the viewport subtree to its scroll owner.
struct scrollbar_style
{
    element_handle viewport{}; ///< Exact live scroll-owner identity, never a path or pointer.
    scroll_axis axis = scroll_axis::vertical; ///< Logical axis controlled by the track.
    float minimum_thumb = 12;   ///< Finite nonnegative logical minimum, capped by track length.
    bool hide_when_zero = true; ///< Hide the chrome subtree when its axis has no range.
    std::array<float, 4> color{0.7f, 0.7f, 0.7f, 1};         ///< Solid straight-alpha thumb tint.
    bool operator==(const scrollbar_style&) const = default; ///< Exact authored link equality.
};

/// @brief Snapshot-owned sparse authored link, in ascending live-slot order.
struct scrollbar_record
{
    element_handle target; ///< Chrome element identity.
    scrollbar_style style; ///< Copied typed configuration.
};

/// @brief Derived track/thumb state before the element's presentation transform.
struct scrollbar_geometry
{
    element_handle viewport{};                ///< Controlled scroll-owner identity.
    scroll_axis axis = scroll_axis::vertical; ///< Axis shared by paint and pointer input.
    layout_rect track{};                      ///< Chrome element border, in layout coordinates.
    layout_rect thumb{};          ///< Finite contained thumb; zero-range uses the entire track.
    bool visible = false;         ///< Includes the zero-range authoring policy.
    bool enabled = false;         ///< Positive range and nonzero finite track travel.
    std::array<float, 4> color{}; ///< Retained thumb tint.
    bool operator==(const scrollbar_geometry&) const = default; ///< Exact numeric comparison.
};

/// @brief Sparse scrollbar output with full identity validation.
struct scrollbar_item
{
    element_handle target;       ///< Chrome element identity.
    scrollbar_geometry geometry; ///< Derived immutable presentation.
};

/// @brief Derive proportional thumb geometry without division by zero or live tree access.
scrollbar_geometry resolve_scrollbar(layout_rect track, const scroll_geometry& scroll,
                                     const scrollbar_style& style);
} // namespace atlas::ui
