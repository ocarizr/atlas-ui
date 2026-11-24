/** @file @brief Shared numeric grid admission without native or editor dependencies. */
#pragma once
#include <atlas/ui/grid_layout.hpp>

namespace atlas::ui::detail
{
inline constexpr std::uint32_t max_grid_cells = 65536; ///< Per-panel logical occupancy admission.
void validate_grid(
    const grid_definition& value); ///< Validate policies and bounded explicit topology.
/// @brief Reject zero spans, overflow and coordinates outside explicit axes.
void validate_grid_slot(const grid_slot& value, const grid_definition& parent);
} // namespace atlas::ui::detail
