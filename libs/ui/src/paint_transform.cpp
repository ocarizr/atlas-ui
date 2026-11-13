/** @file @brief Affine operations preserve a short translation path and checked output. */
#include "paint_transform.hpp"
#include "layout_validation.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace atlas::ui::detail
{
namespace
{
/// @brief Invert viewport coordinates directly into the original border-local space.
std::pair<double, double> inverse(const paint_transform& map, layout_rect border,
                                  layout_point point)
{
    if (map.translation_only())
    {
        const auto origin_x = static_cast<float>(border.x + map.x);
        const auto origin_y = static_cast<float>(border.y + map.y);
        return {static_cast<double>(point.x) - origin_x, static_cast<double>(point.y) - origin_y};
    }

    const auto determinant = map.xx * map.yy - map.xy * map.yx;
    const auto dx = point.x - map.x;
    const auto dy = point.y - map.y;
    return {(map.yy * dx - map.xy * dy) / determinant - border.x,
            (map.xx * dy - map.yx * dx) / determinant - border.y};
}

/// @brief Exact right angles retain rectangular masks and stable edge coordinates.
std::pair<double, double> direction(float degrees)
{
    const auto angle = std::remainder(static_cast<double>(degrees), 360.);
    if (angle == 0)
    {
        return {1, 0};
    }

    if (angle == 90)
    {
        return {0, 1};
    }

    if (angle == -90)
    {
        return {0, -1};
    }

    if (std::abs(angle) == 180)
    {
        return {-1, 0};
    }

    const auto radians = angle * std::numbers::pi / 180.;
    return {std::cos(radians), std::sin(radians)};
}
} // namespace

/// @brief A scissor can represent axes that are preserved or exactly exchanged.
bool paint_transform::axes_preserved() const noexcept
{
    return (xy == 0 && yx == 0) || (xx == 0 && yy == 0);
}

/// @brief Recognize the common path with no rotation or scale.
bool paint_transform::translation_only() const noexcept
{
    return xx == 1 && yy == 1 && xy == 0 && yx == 0;
}

/// @brief Reject nonfinite or unrepresentable coordinates before publishing a packet.
layout_point paint_transform::project(layout_point point) const
{
    return {layout_float(xx * point.x + xy * point.y + x),
            layout_float(yx * point.x + yy * point.y + y)};
}

/// @brief Preserve dimensions for translation-only geometry, including large logical coordinates.
layout_rect paint_transform::bounds(layout_rect border) const
{
    if (translation_only())
    {
        const auto at = project({border.x, border.y});
        return {at.x, at.y, border.width, border.height};
    }

    const auto end_x = layout_float(static_cast<double>(border.x) + border.width);
    const auto end_y = layout_float(static_cast<double>(border.y) + border.height);
    const auto a = project({border.x, border.y});
    const auto b = project({end_x, border.y});
    const auto c = project({border.x, end_y});
    const auto d = project({end_x, end_y});
    const auto [left, right] = std::minmax({a.x, b.x, c.x, d.x});
    const auto [top, bottom] = std::minmax({a.y, b.y, c.y, d.y});
    return {left, top, layout_float(static_cast<double>(right) - left),
            layout_float(static_cast<double>(bottom) - top)};
}

/// @brief A collapsed transform has no meaningful local coordinates.
bool paint_transform::invertible() const noexcept
{
    const auto determinant = xx * yy - xy * yx;
    return std::isfinite(determinant) && determinant != 0;
}

/// @brief A retained capture on a collapsed border receives a neutral local position.
layout_point paint_transform::local(layout_rect border, layout_point point) const
{
    if (!invertible())
    {
        return {};
    }

    const auto [local_x, local_y] = inverse(*this, border, point);
    return {layout_float(local_x), layout_float(local_y)};
}

/// @brief Test the true border in local space even when its viewport hull is much larger.
bool paint_transform::hit(layout_rect border, layout_point point) const noexcept
{
    if (!invertible())
    {
        return false;
    }

    const auto [local_x, local_y] = inverse(*this, border, point);
    return local_x >= 0 && local_y >= 0 && local_x < border.width && local_y < border.height;
}

/// @brief Compose around the original layout pivot without changing layout inputs.
paint_transform compose(const paint_transform& parent, layout_rect border,
                        const element_record& node)
{
    const auto& style = node.paint;
    if (style.scale == layout_point{1, 1} && style.rotation == 0)
    {
        auto result = parent;
        if (parent.translation_only())
        {
            result.x = layout_float(parent.x + style.translation.x);
            result.y = layout_float(parent.y + style.translation.y);
        }
        else
        {
            result.x += parent.xx * style.translation.x + parent.xy * style.translation.y;
            result.y += parent.yx * style.translation.x + parent.yy * style.translation.y;
        }

        return result;
    }

    const auto pivot = resolve_transform_origin(border, node.visual);
    const auto [cosine, sine] = direction(style.rotation);
    const auto xx = cosine * style.scale.x;
    const auto xy = -sine * style.scale.y;
    const auto yx = sine * style.scale.x;
    const auto yy = cosine * style.scale.y;
    const auto x = static_cast<double>(style.translation.x) + pivot.x - xx * pivot.x - xy * pivot.y;
    const auto y = static_cast<double>(style.translation.y) + pivot.y - yx * pivot.x - yy * pivot.y;
    return {parent.xx * xx + parent.xy * yx,          parent.xx * xy + parent.xy * yy,
            parent.yx * xx + parent.yy * yx,          parent.yx * xy + parent.yy * yy,
            parent.xx * x + parent.xy * y + parent.x, parent.yx * x + parent.yy * y + parent.y};
}
} // namespace atlas::ui::detail
