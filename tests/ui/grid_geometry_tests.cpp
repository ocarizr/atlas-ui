/** @file @brief Grid geometry uses explicit track policies and parent-owned cells. */
#include "layout_test_support.hpp"
#include "paint_test_support.hpp"
#include <atlas/ui/grid_layout.hpp>

namespace atlas::ui::tests
{
/// @brief Fixed tracks reserve space before weighted tracks share the remaining viewport.
TEST_F(paint_fixture, grid_distributes_remaining_space_by_track_weight)
{
    const auto root = tree.create();
    grid_definition definition;
    definition.columns = {
        {grid_unit::fixed, 20}, {grid_unit::fraction, 1}, {grid_unit::fraction, 3}};
    definition.rows = {{grid_unit::fixed, 30}};
    definition.column_gap = 5;
    tree.set_panel(root, grid_layout{std::move(definition)});
    const auto first = rectangle(root);
    const auto second = rectangle(root);
    const auto third = rectangle(root);
    tree.set_viewport({0, 0, 100, 80});
    const auto result = layout.calculate(tree.snapshot());
    EXPECT_EQ(require_geometry(result.find(first)).bounds, (layout_rect{0, 0, 20, 30}));
    EXPECT_EQ(require_geometry(result.find(second)).bounds, (layout_rect{25, 0, 17.5f, 30}));
    EXPECT_EQ(require_geometry(result.find(third)).bounds, (layout_rect{47.5f, 0, 52.5f, 30}));
}

/// @brief A default grid grows rows for heterogeneous children without synthesizing widget data.
TEST_F(paint_fixture, grid_implicit_rows_measure_heterogeneous_children)
{
    const auto root = tree.create();
    tree.set_panel(root, grid_layout{});
    const auto first = rectangle(root);
    const auto second = rectangle(root);
    auto style = tree.layout(first);
    style.intrinsic = {20, 12};
    tree.set_layout(first, style);
    style.intrinsic = {30, 18};
    tree.set_layout(second, style);
    tree.set_viewport({0, 0, 100, 80});
    const auto result = layout.calculate(tree.snapshot());
    EXPECT_EQ(require_geometry(result.find(root)).desired, (layout_extent{30, 30}));
    EXPECT_EQ(require_geometry(result.find(first)).bounds, (layout_rect{0, 0, 100, 12}));
    EXPECT_EQ(require_geometry(result.find(second)).bounds, (layout_rect{0, 12, 100, 18}));
}

/// @brief Explicit spanning slots are distinct from canvas anchors and stack order overrides.
TEST_F(paint_fixture, grid_spans_include_internal_gaps)
{
    const auto root = tree.create();
    grid_definition definition;
    definition.columns = {{grid_unit::fixed, 20}, {grid_unit::fixed, 30}};
    definition.rows = {{grid_unit::fixed, 10}, {grid_unit::fixed, 15}};
    definition.column_gap = 4;
    definition.row_gap = 3;
    tree.set_panel(root, grid_layout{std::move(definition)});
    const auto child = rectangle(root);
    tree.set_grid_slot(child, {grid_cell{0, 0}, 2, 2});
    tree.set_viewport({0, 0, 100, 80});
    const auto result = layout.calculate(tree.snapshot());
    EXPECT_EQ(require_geometry(result.find(child)).bounds, (layout_rect{0, 0, 54, 28}));
}

/// @brief Empty declared tracks retain explicit dimensions; an empty implicit axis stays empty.
TEST_F(paint_fixture, grid_empty_fixed_topology_retains_declared_size)
{
    const auto root = tree.create();
    grid_definition policy;
    policy.rows = {{grid_unit::fixed, 12}, {grid_unit::fixed, 18}};
    policy.columns = {{grid_unit::fixed, 20}, {grid_unit::fixed, 30}};
    policy.column_gap = 4;
    policy.row_gap = 3;
    tree.set_panel(root, grid_layout{std::move(policy)});
    tree.set_viewport({0, 0, 100, 80});
    EXPECT_EQ(require_geometry(layout.calculate(tree.snapshot()).find(root)).desired,
              (layout_extent{54, 33}));
    tree.set_panel(root, grid_layout{});
    EXPECT_EQ(require_geometry(layout.calculate(tree.snapshot()).find(root)).desired,
              (layout_extent{}));
}

/// @brief Natural spanning deficits are distributed deterministically over flexible tracks.
TEST_F(paint_fixture, grid_auto_tracks_reduce_spans_after_single_cell_contributions)
{
    const auto root = tree.create();
    grid_definition policy;
    policy.columns = {{}, {}};
    policy.column_gap = 4;
    tree.set_panel(root, grid_layout{std::move(policy)});
    const auto a = rectangle(root);
    const auto b = rectangle(root);
    const auto span = rectangle(root);
    auto style = tree.layout(a);
    style.intrinsic = {20, 10};
    tree.set_layout(a, style);
    style.intrinsic = {30, 12};
    tree.set_layout(b, style);
    style.intrinsic = {74, 8};
    tree.set_layout(span, style);
    tree.set_grid_slot(span, {grid_cell{1, 0}, 1, 2});
    tree.set_viewport({0, 0, 100, 80});
    const auto geometry = layout.calculate(tree.snapshot());
    EXPECT_EQ(require_geometry(geometry.find(root)).desired, (layout_extent{74, 20}));
    EXPECT_EQ(require_geometry(geometry.find(a)).bounds.width, 30);
    EXPECT_EQ(require_geometry(geometry.find(b)).bounds.width, 40);
    EXPECT_EQ(require_geometry(geometry.find(span)).bounds.width, 74);
}

/// @brief Cell placement keeps existing padding, outer margins and child alignment semantics.
TEST_F(paint_fixture, grid_applies_padding_margin_and_alignment)
{
    const auto root = tree.create();
    tree.set_panel(root, grid_layout{});
    auto outer = tree.layout(root);
    outer.padding = {2, 3, 4, 5};
    tree.set_layout(root, outer);
    const auto child = rectangle(root);
    auto style = tree.layout(child);
    style.intrinsic = {20, 10};
    style.margin = {1, 2, 3, 4};
    style.horizontal = layout_alignment::center;
    tree.set_layout(child, style);
    tree.set_viewport({0, 0, 100, 80});
    const auto result = layout.calculate(tree.snapshot());
    EXPECT_EQ(require_geometry(result.find(root)).desired, (layout_extent{30, 24}));
    EXPECT_EQ(require_geometry(result.find(child)).bounds, (layout_rect{38, 5, 20, 10}));
}

/// @brief A one-column grid has the same numeric output as the existing vertical reference.
TEST_F(paint_fixture, grid_one_column_matches_heterogeneous_stack_reference)
{
    const auto grid = tree.create();
    grid_definition definition;
    definition.row_gap = 3;
    tree.set_panel(grid, grid_layout{std::move(definition)});
    const auto stack = tree.create();
    tree.set_panel(stack, vertical_layout{3});
    std::vector<std::pair<element_handle, element_handle>> pairs;
    std::uint32_t seed = 95;
    for (unsigned i = 0; i < 50; ++i)
    {
        seed = seed * 1664525U + 1013904223U;
        const layout_extent desired{static_cast<float>(10 + seed % 40),
                                    static_cast<float>(4 + seed % 20)};
        const auto a = rectangle(grid);
        const auto b = rectangle(stack);
        layout_style style;
        style.intrinsic = desired;
        tree.set_layout(a, style);
        tree.set_layout(b, style);
        pairs.emplace_back(a, b);
    }

    tree.set_viewport({0, 0, 100, 80});
    const auto result = layout.calculate(tree.snapshot());
    EXPECT_EQ(require_geometry(result.find(grid)), require_geometry(result.find(stack)));
    for (const auto& [a, b] : pairs)
    {
        EXPECT_EQ(require_geometry(result.find(a)), require_geometry(result.find(b)));
    }
}

/// @brief One weighted row with implicit columns follows ordinary horizontal-stack geometry.
TEST_F(paint_fixture, grid_implicit_columns_provide_horizontal_layout)
{
    const auto root = tree.create();
    grid_definition definition;
    definition.columns.clear();
    definition.rows = {{grid_unit::fraction, 1}};
    definition.column_gap = 3;
    tree.set_panel(root, grid_layout{std::move(definition)});
    const auto first = rectangle(root);
    const auto second = rectangle(root);
    auto style = tree.layout(first);
    style.intrinsic = {20, 12};
    tree.set_layout(first, style);
    style.intrinsic = {30, 18};
    tree.set_layout(second, style);
    tree.set_viewport({0, 0, 100, 80});
    const auto result = layout.calculate(tree.snapshot());
    EXPECT_EQ(require_geometry(result.find(root)).desired, (layout_extent{53, 18}));
    EXPECT_EQ(require_geometry(result.find(first)).bounds, (layout_rect{0, 0, 20, 80}));
    EXPECT_EQ(require_geometry(result.find(second)).bounds, (layout_rect{23, 0, 30, 80}));
}
} // namespace atlas::ui::tests
