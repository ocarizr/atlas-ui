/** @file @brief Version-seven grid tracks and sparse child metadata with bounded sequences. */
#pragma once
#include "prepared_binary.hpp"

namespace atlas::ui_content::detail
{
/// @brief Immutable definitions are reconstructed only after complete bounded descriptor decoding.
template <class Archive> void grid_fields(Archive& format, ui::grid_layout& grid)
{
    ui::grid_definition definition;
    if constexpr (!Archive::reading)
    {
        definition = grid.definition();
    }

    const auto track = [](auto& archive, ui::grid_track& value)
    {
        archive.choice(value.unit, ui::grid_unit::fraction);
        archive.field(value.value);
    };
    format.sequence(definition.columns, 65536, track);
    format.sequence(definition.rows, 65536, track);
    format.fields(definition.column_gap, definition.row_gap);
    if constexpr (Archive::reading)
    {
        grid = ui::grid_layout{std::move(definition)};
    }
}

/// @brief Explicit positions remain optional; automatic spans survive preparation without
/// expansion.
template <class Archive> void grid_slot_fields(Archive& format, ui::grid_slot& slot)
{
    format.optional(slot.position,
                    [](auto& archive, ui::grid_cell& cell)
                    {
                        archive.fields(cell.row, cell.column);
                    });
    format.fields(slot.row_span, slot.column_span);
}
} // namespace atlas::ui_content::detail
