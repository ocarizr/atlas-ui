/**
 * @file
 * @brief Logical layout units, constraints and the basic container vocabulary.
 */
#pragma once
#include <atlas/ui/commands.hpp>
#include <limits>

namespace atlas::ui
{
/// @brief Finite nonnegative logical extents, independent of physical pixels.
struct layout_extent
{
    float width = 0;                                       ///< Horizontal extent.
    float height = 0;                                      ///< Vertical extent.
    bool operator==(const layout_extent&) const = default; ///< Compare both axes.
};

/// @brief Logical border rectangle; positions may be negative, extents may not.
struct layout_rect
{
    float x = 0;                                         ///< Left position in viewport coordinates.
    float y = 0;                                         ///< Top position in viewport coordinates.
    float width = 0;                                     ///< Border width.
    float height = 0;                                    ///< Border height.
    bool operator==(const layout_rect&) const = default; ///< Compare numeric geometry.
};

/// @brief Nonnegative finite outer margins or inside-border padding.
struct layout_insets
{
    float left = 0;                                        ///< Leading horizontal inset.
    float top = 0;                                         ///< Leading vertical inset.
    float right = 0;                                       ///< Trailing horizontal inset.
    float bottom = 0;                                      ///< Trailing vertical inset.
    bool operator==(const layout_insets&) const = default; ///< Compare every edge.
};

/// @brief Border-size bounds; only maxima may use positive infinity.
struct layout_constraints
{
    layout_extent minimum{}; ///< Finite lower bound on each axis.
    layout_extent maximum{std::numeric_limits<float>::infinity(),
                          std::numeric_limits<float>::infinity()}; ///< Unbounded by default.
    bool operator==(const layout_constraints&) const = default;    ///< Compare both bounds.
};
/// @brief Own alignment inside a parent slot, after margins.
enum class layout_alignment : std::uint8_t
{
    stretch, ///< Fill available space unless the axis has a fixed request.
    start,   ///< Natural desired size at the leading edge.
    center,  ///< Natural desired size centered in the slot.
    end      ///< Natural desired size at the trailing edge.
};

/// @brief Geometry-affecting properties; width/height requests remain in command state.
struct layout_style
{
    layout_extent intrinsic{}; ///< Minimum natural content hint before padding and constraints.
    layout_insets padding{};   ///< Inside-border content insets.
    layout_insets margin{};    ///< Outside-border space within a parent slot.
    layout_constraints constraints{};                        ///< Bounds on border size.
    layout_alignment horizontal = layout_alignment::stretch; ///< Own horizontal alignment.
    layout_alignment vertical = layout_alignment::stretch;   ///< Own vertical alignment.
    bool operator==(const layout_style&) const = default;    ///< Exact cache input comparison.
};

/// @brief Measured natural size and arranged border rectangle.
struct layout_geometry
{
    layout_extent desired; ///< Size after requested overrides and own constraints.
    layout_rect bounds;    ///< Final slot-constrained border rectangle.
    bool operator==(const layout_geometry&) const = default; ///< Compare computed state.
};

/// @brief Immutable numeric value associated with a generation-bearing identity.
struct layout_item
{
    element_handle target;    ///< Exact captured identity.
    layout_geometry geometry; ///< Owned numeric output without registry pointers.
    bool operator==(const layout_item&) const = default; ///< Compare identity and geometry.
};
} // namespace atlas::ui
