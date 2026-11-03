/** @file @brief Shared finite-policy and canvas-axis exclusivity checks. */
#pragma once
#include <atlas/ui/layout_boundary.hpp>

namespace atlas::ui::detail
{
/// @brief Axis placement is authored either by canvas anchors or by a sibling relationship.
void validate_boundary_values(const layout_boundaries& value, const canvas_slot& slot);
} // namespace atlas::ui::detail
