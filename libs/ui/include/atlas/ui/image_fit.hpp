/**
 * @file
 * @brief Pure image aspect fitting in logical layout and normalized texture coordinates.
 */
#pragma once
#include <atlas/ui/layout_types.hpp>

namespace atlas::ui
{
/// @brief Explicit aspect policy independent from source loading.
enum class image_fit : std::uint8_t
{
    stretch, ///< Fill the border without preserving source aspect.
    contain, ///< Center a fully visible source inside the border.
    cover    ///< Fill the border and crop normalized UVs symmetrically.
};

struct image_sampling
{
    layout_extent source; ///< Positive finite source dimensions, independent of decoded storage.
    image_fit fit = image_fit::stretch;                     ///< Explicit aspect policy.
    bool operator==(const image_sampling&) const = default; ///< Compare typed image parameters.
}; ///< Optional paint metadata, separate from common layout requests.

struct image_placement
{
    layout_rect bounds; ///< Fitted logical rectangle.
    layout_rect uv;     ///< Selected source region after aspect crop.
}; ///< Numeric output contains no resource, codec or platform dependency.

/// @brief Validate and resolve a centered image into logical bounds and source UVs.
image_placement fit_image(layout_rect bounds, image_sampling sampling,
                          layout_rect uv = {0, 0, 1, 1});
///< Validate inputs and resolve centered aspect fitting, including zero destination extents.
} // namespace atlas::ui
