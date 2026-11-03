/** @file @brief Deterministic finite geometry guards without native/platform dependencies. */
#include "layout_validation.hpp"
#include <cmath>

namespace atlas::ui::detail
{
namespace
{
/// @brief Lower bounds and content hints never use infinity or negative dimensions.
bool nonnegative(float value)
{
    return std::isfinite(value) && value >= 0;
}

/// @brief Maxima alone can express an unbounded axis.
bool bound(float minimum, float maximum)
{
    return nonnegative(minimum) && !std::isnan(maximum) && maximum >= minimum;
}

/// @brief Insets have no hidden negative-margin or overflow compensation behavior.
bool insets(layout_insets value)
{
    return nonnegative(value.left) && nonnegative(value.top) && nonnegative(value.right) &&
           nonnegative(value.bottom);
}

/// @brief Unknown enum values cannot silently become a different alignment mode.
bool alignment(layout_alignment value)
{
    return value == layout_alignment::stretch || value == layout_alignment::start ||
           value == layout_alignment::center || value == layout_alignment::end;
}
} // namespace

/// @brief Logical results use doubles during accumulation, with checked conversion at boundaries.
float layout_float(double value)
{
    if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
    {
        throw std::overflow_error("UI layout arithmetic exceeded finite float geometry");
    }

    return static_cast<float>(value);
}

/// @brief Validation precedes live state mutation.
void validate_style(const layout_style& value)
{
    if ((value.kind != layout_kind::leaf && value.kind != layout_kind::size_box &&
         value.kind != layout_kind::overlay && value.kind != layout_kind::horizontal &&
         value.kind != layout_kind::vertical) ||
        !nonnegative(value.intrinsic.width) || !nonnegative(value.intrinsic.height) ||
        !nonnegative(value.spacing) || !insets(value.padding) || !insets(value.margin) ||
        !bound(value.constraints.minimum.width, value.constraints.maximum.width) ||
        !bound(value.constraints.minimum.height, value.constraints.maximum.height) ||
        !alignment(value.horizontal) || !alignment(value.vertical))
    {
        throw std::invalid_argument("invalid UI layout style or constraints");
    }
}

/// @brief The root's right/bottom coordinates must be representable too.
void validate_viewport(layout_rect value)
{
    if (!std::isfinite(value.x) || !std::isfinite(value.y) || !nonnegative(value.width) ||
        !nonnegative(value.height) ||
        std::abs(static_cast<double>(value.x) + value.width) > std::numeric_limits<float>::max() ||
        std::abs(static_cast<double>(value.y) + value.height) > std::numeric_limits<float>::max())
    {
        throw std::invalid_argument("invalid finite UI viewport");
    }
}
} // namespace atlas::ui::detail
