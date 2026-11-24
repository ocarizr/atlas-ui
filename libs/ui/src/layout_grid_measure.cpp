/** @file @brief Deterministic track sizing from single-cell and spanning child contributions. */
#include "layout_grid.hpp"
#include <numeric>

namespace atlas::ui::detail
{
/// @brief Checked callers retain precise accumulation before final float publication.
double grid_extent(std::span<const double> sizes, float gap)
{
    return std::accumulate(sizes.begin(), sizes.end(), 0.) +
           static_cast<double>(sizes.empty() ? 0 : sizes.size() - 1) * gap;
}

namespace
{
/// @brief Implicit tracks use automatic sizing and do not allocate descriptor objects.
grid_track track(std::span<const grid_track> declared, std::size_t index)
{
    return declared.empty() ? grid_track{} : declared[index];
}

/// @brief Single-cell contributions precede spans; each pass preserves source child order.
void contribute(const measured_branch& measured, std::uint32_t parent, bool horizontal,
                std::span<double> sizes, const std::stop_token& stop)
{
    const auto& source = *measured.source;
    const auto& policy = std::get<grid_layout>(source.nodes[parent].panel).definition();
    const auto& declared = horizontal ? policy.columns : policy.rows;
    const auto gap = horizontal ? policy.column_gap : policy.row_gap;
    for (const bool spanning : {false, true})
    {
        for (auto child = source.nodes[parent].first; child != no_layout_node;
             child = source.nodes[child].next)
        {
            if (stop.stop_requested())
            {
                throw jobs::job_cancelled{};
            }

            const auto& slot = source.grid_slots[source.nodes[child].slot];
            const auto cell = slot.position.value_or(grid_cell{});
            const auto first = horizontal ? cell.column : cell.row;
            const auto count = horizontal ? slot.column_span : slot.row_span;
            if ((count > 1) != spanning)
            {
                continue;
            }

            const auto margin = source.nodes[child].style.margin;
            const auto desired = measured.desired[child];
            const double wanted =
                horizontal ? static_cast<double>(desired.width) + margin.left + margin.right
                           : static_cast<double>(desired.height) + margin.top + margin.bottom;
            double current = static_cast<double>(count - 1) * gap;
            double weight = 0;
            for (auto index = first; index < first + count; ++index)
            {
                current += sizes[index];
                const auto entry = track(declared, index);
                if (entry.unit != grid_unit::fixed)
                {
                    weight += entry.unit == grid_unit::fraction ? entry.value : 1;
                }
            }

            if (wanted <= current || weight == 0)
            {
                continue;
            }

            for (auto index = first; index < first + count; ++index)
            {
                const auto entry = track(declared, index);
                if (entry.unit != grid_unit::fixed)
                {
                    sizes[index] += (wanted - current) *
                                    (entry.unit == grid_unit::fraction ? entry.value : 1) / weight;
                }
            }
        }
    }
}
} // namespace

/// @brief Completed child outputs are immutable; scratch is not shared with sibling jobs.
grid_tracks::grid_tracks(const measured_branch& measured, std::uint32_t parent,
                         const std::stop_token& stop)
    : rows(0, measured.source->memory.get()), columns(0, measured.source->memory.get())
{
    const auto& source = *measured.source;
    const auto& policy = std::get<grid_layout>(source.nodes[parent].panel).definition();
    auto row_count = policy.rows.size();
    auto column_count = policy.columns.size();
    for (auto child = source.nodes[parent].first; child != no_layout_node;
         child = source.nodes[child].next)
    {
        const auto& slot = source.grid_slots[source.nodes[child].slot];
        const auto cell = slot.position.value_or(grid_cell{});
        row_count = std::max(row_count, std::size_t{cell.row} + slot.row_span);
        column_count = std::max(column_count, std::size_t{cell.column} + slot.column_span);
    }

    rows.resize(row_count, 0);
    columns.resize(column_count, 0);
    for (std::size_t i = 0; i < row_count; ++i)
    {
        if (track(policy.rows, i).unit == grid_unit::fixed)
        {
            rows[i] = track(policy.rows, i).value;
        }
    }

    for (std::size_t i = 0; i < column_count; ++i)
    {
        if (track(policy.columns, i).unit == grid_unit::fixed)
        {
            columns[i] = track(policy.columns, i).value;
        }
    }

    contribute(measured, parent, true, columns, stop);
    contribute(measured, parent, false, rows, stop);
}

/// @brief Grid natural size is the finite sum of tracks and internal gaps.
std::pair<double, double> measure_grid(const measured_branch& measured, std::uint32_t parent,
                                       const std::stop_token& stop)
{
    const grid_tracks tracks{measured, parent, stop};
    const auto& policy = std::get<grid_layout>(measured.source->nodes[parent].panel).definition();
    return {grid_extent(tracks.columns, policy.column_gap),
            grid_extent(tracks.rows, policy.row_gap)};
}
} // namespace atlas::ui::detail
