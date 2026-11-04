/**
 * @file
 * @brief Optional platform-neutral rectangle appearance and parent-local stacking.
 */
#pragma once
#include <array>
#include <atlas/ui/visual_style.hpp>

namespace atlas::ui
{
struct paint_style
{
    bool fill = false; ///< Draw this border rectangle; containers need not have a background.
    bool clip = false; ///< Intersect this translated border with the inherited rectangular clip.
    std::int32_t layer = 0; ///< Smaller is foreground; each element isolates its descendants.
    std::array<float, 4> color{1, 1, 1, 1}; ///< Straight-alpha linear RGBA in [0,1].
    std::uint64_t texture = 0;  ///< Zero means solid; nonzero is a consumer-resolved resident key.
    layout_rect uv{0, 0, 1, 1}; ///< Finite normalized texture region, nonnegative extent.
    layout_point translation{}; ///< Finite paint-only offset inherited by descendants.
    bool operator==(const paint_style&) const = default; ///< Exact appearance comparison.
}; ///< Sparse component; no graphics resource or platform SDK dependency.
} // namespace atlas::ui
