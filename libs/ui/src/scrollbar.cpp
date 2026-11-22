/** @file @brief Checked proportional scrollbar geometry without backend or input ownership. */
#include "layout_validation.hpp"
#include <algorithm>
#include <atlas/ui/scrollbar.hpp>
#include <cmath>

namespace atlas::ui
{
/// @brief Finite track travel and clamped offsets avoid zero-range division and overscroll.
scrollbar_geometry resolve_scrollbar(layout_rect track, const scroll_geometry& scroll,
                                     const scrollbar_style& style)
{
    detail::validate_viewport(track);
    detail::validate_viewport(scroll.viewport);
    if (style.axis > scroll_axis::vertical || !std::isfinite(style.minimum_thumb) ||
        style.minimum_thumb < 0)
    {
        throw std::invalid_argument("invalid scrollbar axis or minimum thumb");
    }

    for (const auto value : style.color)
    {
        if (!std::isfinite(value) || value < 0 || value > 1)
        {
            throw std::invalid_argument("invalid scrollbar thumb color");
        }
    }

    const bool horizontal = style.axis == scroll_axis::horizontal;
    const auto length = horizontal ? track.width : track.height;
    const auto extent = horizontal ? scroll.extent.width : scroll.extent.height;
    const auto visible = horizontal ? scroll.viewport.width : scroll.viewport.height;
    const auto range = horizontal ? scroll.range.x : scroll.range.y;
    const auto offset = horizontal ? scroll.offset.x : scroll.offset.y;
    if (!std::isfinite(extent) || extent < 0 || !std::isfinite(range) || range < 0 ||
        !std::isfinite(offset) || offset < 0 || offset > range)
    {
        throw std::invalid_argument("invalid scrollbar scroll geometry");
    }

    const auto size = range > 0 && extent > 0
                          ? std::clamp(static_cast<double>(length) * visible / extent,
                                       static_cast<double>(std::min(length, style.minimum_thumb)),
                                       static_cast<double>(length))
                          : static_cast<double>(length);
    const auto thumb_length = detail::layout_float(size);
    const auto travel = static_cast<double>(length) - thumb_length;
    const auto advance = range > 0 ? travel * offset / range : 0;
    auto thumb = track;
    if (horizontal)
    {
        thumb.x = detail::layout_float(static_cast<double>(track.x) + advance);
        thumb.width = thumb_length;
    }
    else
    {
        thumb.y = detail::layout_float(static_cast<double>(track.y) + advance);
        thumb.height = thumb_length;
    }

    return {style.viewport,
            style.axis,
            track,
            thumb,
            !style.hide_when_zero || range > 0,
            range > 0 && travel > 0 && track.width > 0 && track.height > 0,
            style.color};
}
} // namespace atlas::ui
