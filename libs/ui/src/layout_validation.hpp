/** @file @brief Shared validation of trusted layout parameters and numeric output. */
#pragma once
#include <atlas/ui/layout_types.hpp>

namespace atlas::ui::detail
{
void validate_style(
    const layout_style& value);            ///< Reject invalid enums, extents, insets and bounds.
void validate_viewport(layout_rect value); ///< Reject negative extents and unrepresentable edges.
float layout_float(double value); ///< Reject arithmetic overflow instead of emitting NaN/infinity.
} // namespace atlas::ui::detail
