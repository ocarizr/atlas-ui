/** @file @brief Immutable validated track ownership and exact value comparison. */
#include "grid_validation.hpp"
#include <cmath>

namespace atlas::ui
{
/// @brief Policy validation precedes allocation/publication of shared track ownership.
grid_layout::grid_layout(grid_definition definition)
{
    detail::validate_grid(definition);
    definition_ = std::make_shared<const grid_definition>(std::move(definition));
}

/// @brief The default is shared without allocating per default grid instance.
const grid_definition& grid_layout::definition() const
{
    static const grid_definition defaults;
    return definition_ ? *definition_ : defaults;
}

/// @brief Equivalent re-created definitions do not spuriously invalidate layout caches.
bool grid_layout::operator==(const grid_layout& other) const
{
    return definition_ == other.definition_ || definition() == other.definition();
}

/// @brief Reuse the same numeric guards in runtime and content authoring boundaries.
void validate_grid_slot(const grid_slot& slot, const grid_layout& parent)
{
    detail::validate_grid_slot(slot, parent.definition());
}
} // namespace atlas::ui

namespace atlas::ui::detail
{
/// @brief Explicit cells cannot request an unbounded occupancy map or invalid track arithmetic.
void validate_grid(const grid_definition& value)
{
    if ((value.columns.empty() && value.rows.empty()) || value.columns.size() > max_grid_cells ||
        value.rows.size() > max_grid_cells ||
        (!value.columns.empty() && value.rows.size() > max_grid_cells / value.columns.size()) ||
        !std::isfinite(value.column_gap) || value.column_gap < 0 || !std::isfinite(value.row_gap) ||
        value.row_gap < 0)
    {
        throw std::invalid_argument("invalid grid topology or spacing");
    }

    for (const auto& tracks :
         {std::span<const grid_track>{value.rows}, std::span<const grid_track>{value.columns}})
    {
        for (const auto& track : tracks)
        {
            if (!std::isfinite(track.value) || track.value < 0 ||
                static_cast<unsigned>(track.unit) > static_cast<unsigned>(grid_unit::fraction) ||
                (track.unit == grid_unit::fraction && track.value == 0) ||
                (track.unit == grid_unit::automatic && track.value != 0))
            {
                throw std::invalid_argument("invalid grid track policy");
            }
        }
    }
}

/// @brief Validate local placement before any sparse metadata or layout revision changes.
void validate_grid_slot(const grid_slot& value, const grid_definition& parent)
{
    const auto row = value.position.value_or(grid_cell{}).row;
    const auto column = value.position.value_or(grid_cell{}).column;
    if (!value.row_span || !value.column_span || value.row_span > max_grid_cells ||
        value.column_span > max_grid_cells || row >= max_grid_cells || column >= max_grid_cells ||
        std::uint64_t{row} + value.row_span > max_grid_cells ||
        std::uint64_t{column} + value.column_span > max_grid_cells ||
        (!parent.rows.empty() && std::uint64_t{row} + value.row_span > parent.rows.size()) ||
        (!parent.columns.empty() &&
         std::uint64_t{column} + value.column_span > parent.columns.size()))
    {
        throw std::invalid_argument("grid slot is outside its track topology");
    }
}
} // namespace atlas::ui::detail
