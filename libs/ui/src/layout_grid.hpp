/** @file @brief Local grid numeric workspace shared by measure and arrange kernels. */
#pragma once
#include "layout_numeric.hpp"
#include "layout_state.hpp"

namespace atlas::ui::detail
{
double grid_extent(std::span<const double> sizes,
                   float gap); ///< Sum tracks and internal gaps in double precision.

/// @brief Exact row/column contributions, owned only by the invoking kernel.
struct grid_tracks
{
    /// @brief Allocate bounded axis scratch and reduce completed child desired sizes.
    grid_tracks(const measured_branch& measured, std::uint32_t parent, const std::stop_token& stop);
    std::pmr::vector<double> rows;    ///< Natural row extents, including explicit fixed tracks.
    std::pmr::vector<double> columns; ///< Natural column extents.
};

/// @brief Return natural content size with gaps; common padding/constraints apply afterward.
std::pair<double, double> measure_grid(const measured_branch& measured, std::uint32_t parent,
                                       const std::stop_token& stop);
void arrange_grid(
    arranged_branch& result, std::uint32_t parent, slot_rect content,
    const std::stop_token& stop); ///< Write disjoint child cell slots before alignment.
} // namespace atlas::ui::detail
