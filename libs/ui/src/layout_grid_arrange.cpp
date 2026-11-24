/** @file @brief Weighted remaining space and prefix cell slots, without per-cell jobs. */
#include "layout_grid.hpp"
#include <numeric>

namespace atlas::ui::detail
{
namespace
{
/// @brief Distinguish available extent from internal track spacing at call sites.
struct grid_space
{
    double available; ///< Resolved parent content extent.
    float gap;        ///< Authored internal gap.
};

/// @brief Preserve natural/fixed extents on deficit; distribute only positive spare space.
void expand(std::span<double> sizes, std::span<const grid_track> declared, grid_space space)
{
    const auto natural = grid_extent(sizes, space.gap);
    double weights = 0;
    for (const auto& entry : declared)
    {
        if (entry.unit == grid_unit::fraction)
        {
            weights += entry.value;
        }
    }

    if (weights == 0 || space.available <= natural)
    {
        return;
    }

    for (std::size_t i = 0; i < declared.size(); ++i)
    {
        if (declared[i].unit == grid_unit::fraction)
        {
            sizes[i] += (space.available - natural) * declared[i].value / weights;
        }
    }
}

/// @brief Convert extents to prefix starts in place; spans use prefix differences plus final track.
void prefix(std::span<double> sizes, float gap)
{
    double next = 0;
    for (auto& value : sizes)
    {
        const auto extent = value;
        value = next;
        next += extent + gap;
    }
}
} // namespace

/// @brief Track scratch is reused for prefix starts after final cell ends are computed.
void arrange_grid(arranged_branch& result, std::uint32_t parent, slot_rect content,
                  const std::stop_token& stop)
{
    const auto& measured = *result.measured;
    const auto& source = *measured.source;
    const auto& policy = std::get<grid_layout>(source.nodes[parent].panel).definition();
    grid_tracks tracks{measured, parent, stop};
    expand(tracks.columns, policy.columns, {content.width, policy.column_gap});
    expand(tracks.rows, policy.rows, {content.height, policy.row_gap});
    const auto width = grid_extent(tracks.columns, policy.column_gap);
    const auto height = grid_extent(tracks.rows, policy.row_gap);
    prefix(tracks.columns, policy.column_gap);
    prefix(tracks.rows, policy.row_gap);
    for (auto child = source.nodes[parent].first; child != no_layout_node;
         child = source.nodes[child].next)
    {
        if (stop.stop_requested())
        {
            throw jobs::job_cancelled{};
        }

        const auto& slot = source.grid_slots[source.nodes[child].slot];
        const auto cell = slot.position.value_or(grid_cell{});
        const auto right = cell.column + slot.column_span;
        const auto bottom = cell.row + slot.row_span;
        const auto x = tracks.columns[cell.column];
        const auto y = tracks.rows[cell.row];
        const auto end_x =
            right == tracks.columns.size() ? width : tracks.columns[right] - policy.column_gap;
        const auto end_y =
            bottom == tracks.rows.size() ? height : tracks.rows[bottom] - policy.row_gap;
        result.items[child].geometry.bounds = {layout_float(content.x + x),
                                               layout_float(content.y + y), layout_float(end_x - x),
                                               layout_float(end_y - y)};
    }
}
} // namespace atlas::ui::detail
