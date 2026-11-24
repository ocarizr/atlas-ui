/** @file @brief Explicit reservations, transposed implicit axes and atomic cell admission. */
#include <atlas/ui/grid_layout.hpp>
#include <gtest/gtest.h>

namespace atlas::ui::tests
{
/// @brief Both implicit orientations share one sparse stable occupancy contract.
class grid_placement_test : public ::testing::TestWithParam<bool>
{
};

/// @brief Later explicit cells are reserved before earlier automatic entries are assigned.
TEST_P(grid_placement_test, reserves_explicit_cells_before_auto_placement)
{
    grid_definition definition;
    const bool horizontal = GetParam();
    if (horizontal)
    {
        definition.rows = {{}};
        definition.columns.clear();
    }

    const grid_layout grid{std::move(definition)};
    const std::array<grid_slot, 3> input{{{}, {grid_cell{0, 0}, 1, 1}, {}}};
    std::array<grid_slot, 3> output;
    resolve_grid_slots(grid, input, output);
    EXPECT_EQ(output[0].position, horizontal ? (grid_cell{0, 1}) : (grid_cell{1, 0}));
    EXPECT_EQ(output[1].position, (grid_cell{0, 0}));
    EXPECT_EQ(output[2].position, horizontal ? (grid_cell{0, 2}) : (grid_cell{2, 0}));
}

/// @brief Stable names identify vertical and horizontal specializations in reports.
INSTANTIATE_TEST_SUITE_P(axes, grid_placement_test, ::testing::Values(false, true),
                         [](const auto& value)
                         {
                             return value.param ? "implicit_columns" : "implicit_rows";
                         });

/// @brief Collision and exhausted topology cannot partially replace caller-owned output.
TEST(grid_placement, rejects_collisions_and_capacity_without_partial_output)
{
    grid_definition definition;
    definition.rows = {{}};
    const grid_layout grid{std::move(definition)};
    std::array<grid_slot, 2> input{{{grid_cell{0, 0}, 1, 1}, {grid_cell{0, 0}, 1, 1}}};
    const std::array<grid_slot, 2> saved{{{grid_cell{9, 9}, 1, 1}, {grid_cell{8, 8}, 1, 1}}};
    auto output = saved;
    EXPECT_THROW(resolve_grid_slots(grid, input, output), std::invalid_argument);
    EXPECT_EQ(output, saved);
    input = {};
    EXPECT_THROW(resolve_grid_slots(grid, input, output), std::overflow_error);
    EXPECT_EQ(output, saved);
}

/// @brief An 8 KiB maximum occupancy map admits its final logical cell without overflow.
TEST(grid_placement, final_logical_cell_and_aliased_output_are_supported)
{
    const grid_layout grid;
    std::array<grid_slot, 1> slots{{{grid_cell{65535, 0}, 1, 1}}};
    resolve_grid_slots(grid, slots, slots);
    EXPECT_EQ(slots[0].position, (grid_cell{65535, 0}));
    slots[0].row_span = 2;
    EXPECT_THROW(resolve_grid_slots(grid, slots, slots), std::invalid_argument);
}

/// @brief A fragmented impossible span cannot consume quadratic work without an explicit bound.
TEST(grid_placement, pathological_span_search_has_bounded_work_and_atomic_output)
{
    std::array<grid_slot, 2> input{{{grid_cell{32768, 0}, 1, 1}, {{}, 32769, 1}}};
    std::array<grid_slot, 2> output{};
    const auto before = output;
    EXPECT_THROW(resolve_grid_slots(grid_layout{}, input, output), std::overflow_error);
    EXPECT_EQ(output, before);
}
} // namespace atlas::ui::tests
