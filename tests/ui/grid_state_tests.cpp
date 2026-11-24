/** @file @brief Sparse cell lifetime, exact cache reuse and transactional layout failures. */
#include "layout_test_support.hpp"
#include "paint_test_support.hpp"
#include <atlas/ui/input_session.hpp>
#include <atlas/ui/virtual_list.hpp>

namespace atlas::ui::tests
{
/// @brief Invalid metadata and failed conversions cannot replace a valid grid relationship.
TEST_F(paint_fixture, grid_slot_validation_and_conversion_preserve_state_on_failure)
{
    const auto root = tree.create();
    tree.set_panel(root, grid_layout{});
    const auto child = rectangle(root);
    const grid_slot slot{grid_cell{3, 0}, 1, 1};
    tree.set_grid_slot(child, slot);
    const auto source = tree.snapshot();
    EXPECT_THROW(tree.set_grid_slot(child, {{}, 0, 1}), std::invalid_argument);
    EXPECT_EQ(tree.snapshot().revision(), source.revision());
    grid_definition fixed;
    fixed.rows = {{}};
    EXPECT_THROW(tree.set_panel(root, grid_layout{std::move(fixed)}), std::invalid_argument);
    EXPECT_EQ(tree.grid_slot_for(child), slot);
    tree.set_panel(root, overlay_layout{});
    EXPECT_FALSE(tree.grid_slot_for(child));
    EXPECT_TRUE(tree.snapshot().grid_slots().empty());
    ASSERT_EQ(source.grid_slots().size(), 1);
    EXPECT_EQ(source.grid_slots()[0].slot, slot);
}

/// @brief Reparent and slot-generation reuse never inherit an unrelated cell assignment.
TEST_F(paint_fixture, grid_parent_change_and_reuse_clear_sparse_metadata)
{
    const auto root = tree.create();
    tree.set_panel(root, grid_layout{});
    const auto outside = tree.create();
    const auto child = rectangle(root);
    tree.set_grid_slot(child, {grid_cell{5, 0}, 1, 1});
    tree.reparent(child, outside);
    EXPECT_FALSE(tree.grid_slot_for(child));
    tree.reparent(child, root);
    EXPECT_EQ(tree.grid_slot_for(child), grid_slot{});
    tree.remove(child);
    const auto replacement = rectangle(root);
    EXPECT_NE(replacement, child);
    EXPECT_EQ(tree.grid_slot_for(replacement), grid_slot{});
}

/// @brief Track/slot changes invalidate numeric inputs; equivalent immutable definitions reuse
/// them.
TEST_F(paint_fixture, grid_cache_compares_actual_track_values_and_cells)
{
    const auto root = tree.create();
    tree.set_panel(root, grid_layout{});
    const auto child = rectangle(root);
    auto style = tree.layout(child);
    style.intrinsic.height = 10;
    tree.set_layout(child, style);
    tree.set_viewport({0, 0, 100, 80});
    const auto first = layout.calculate(tree.snapshot());
    const auto revision = tree.snapshot().revision();
    tree.set_panel(root, grid_layout{grid_definition{}});
    EXPECT_EQ(tree.snapshot().revision(), revision);
    (void)layout.calculate(tree.snapshot());
    EXPECT_EQ(layout.statistics().measured_nodes, 0);
    tree.set_grid_slot(child, {grid_cell{2, 0}, 1, 1});
    (void)layout.calculate(tree.snapshot());
    EXPECT_EQ(layout.statistics().measured_nodes, 2);
    EXPECT_EQ(require_geometry(first.find(child)).bounds.y, 0);
}

/// @brief Collision failure preserves the old immutable packet and allows correction/retry.
TEST_F(paint_fixture, grid_collision_does_not_publish_partial_layout)
{
    const auto root = tree.create();
    tree.set_panel(root, grid_layout{});
    const auto first = rectangle(root);
    const auto second = rectangle(root);
    tree.set_viewport({0, 0, 100, 80});
    const auto before = layout.calculate(tree.snapshot());
    tree.set_grid_slot(first, {grid_cell{0, 0}, 1, 1});
    tree.set_grid_slot(second, {grid_cell{0, 0}, 1, 1});
    EXPECT_THROW((void)layout.calculate(tree.snapshot()), std::invalid_argument);
    EXPECT_EQ(before.items().size(), 3);
    tree.reset_grid_slot(second);
    EXPECT_NO_THROW((void)layout.calculate(tree.snapshot()));
}

/// @brief Virtual row transactions preserve sparse grid metadata on retained nested subtrees.
TEST_F(paint_fixture, grid_cells_survive_virtual_list_transaction_cloning)
{
    const auto viewport = tree.create();
    tree.set_panel(viewport, scroll_layout{});
    input_session input{tree};
    element_handle retained;
    virtual_list list{tree, input, viewport, workers.dispatcher(), [&](row_context& row)
                      {
                          row.tree().set_panel(row.root(), grid_layout{});
                          const auto child = row.tree().create(row.root());
                          row.tree().set_grid_slot(child, {grid_cell{2, 0}, 1, 1});
                          if (row.row().item.key == 1)
                          {
                              retained = child;
                              retained.session = row.row().ticket.root.session;
                          }
                      }};
    auto data = std::make_shared<fixed_item_source>(fixed_item_options{100, 20});
    list.update(data, 80);
    list.request_focus(5);
    list.update(data, 80);
    EXPECT_EQ(tree.grid_slot_for(retained), (grid_slot{grid_cell{2, 0}, 1, 1}));
}
} // namespace atlas::ui::tests
