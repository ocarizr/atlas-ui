/** @file @brief Stable layout order and main-axis distribution without hierarchy mutation. */
#include "layout_test_support.hpp"
#include <atlas/jobs/job_system.hpp>
#include <atlas/ui/element_tree.hpp>
#include <atlas/ui/layout_engine.hpp>
#include <gtest/gtest.h>

namespace atlas::ui::tests
{
/// @brief Each fact owns its persistent dispatcher and owner-affine state.
class stack_state_test : public ::testing::Test
{
  protected:
    jobs::job_system pool{2};                ///< Shared by layout phases.
    element_tree tree;                       ///< Independent session per fact.
    layout_engine engine{pool.dispatcher()}; ///< No extra scheduler.

    element_handle item(element_handle parent, layout_extent extent)
    {
        const auto child = tree.create(parent);
        layout_style style;
        style.intrinsic = extent;
        tree.set_layout(child, style);
        return child;
    } ///< Create a measured child without deferred property writes.
};

/// @brief Equal free-space gaps preserve fixed requests and occupy the available extent.
TEST_F(stack_state_test, space_between_preserves_four_fixed_buttons)
{
    const auto root = tree.create();
    vertical_layout column;
    column.distribution = stack_distribution::space_between;
    column.fill = stack_fill::weighted;
    tree.set_panel(root, column);
    std::vector<element_handle> children;
    children.reserve(4);
    tree.commands().begin_frame(1);
    for (int index = 0; index < 4; ++index)
    {
        const auto child = item(root, {20, 23});
        children.push_back(child);
        tree.commands().set(child, property::height, 23.f, 1);
    }

    tree.apply(tree.commands().seal());
    const auto result = engine.update(tree, {0, 0, 100, 100});
    for (std::size_t index = 0; index < children.size(); ++index)
    {
        const auto bounds = require_geometry(result.find(children[index])).bounds;
        EXPECT_FLOAT_EQ(bounds.height, 23.f);
        EXPECT_FLOAT_EQ(bounds.y, static_cast<float>(index) * (23.f + 8.f / 3.f));
    }
}

/// @brief Favorites can sort first without recreating elements or changing sibling links.
TEST_F(stack_state_test, ordering_is_stable_and_does_not_reparent)
{
    const auto root = tree.create();
    tree.set_panel(root, vertical_layout{});
    const auto first = item(root, {20, 10});
    const auto favorite = item(root, {20, 15});
    const auto second_favorite = item(root, {20, 5});
    stack_slot slot;
    slot.order = -1;
    tree.set_stack_slot(favorite, slot);
    tree.set_stack_slot(second_favorite, slot);
    const auto result = engine.update(tree, {0, 0, 100, 100});
    EXPECT_EQ(require_geometry(result.find(favorite)).bounds.y, 0.f);
    EXPECT_EQ(require_geometry(result.find(second_favorite)).bounds.y, 15.f);
    EXPECT_EQ(require_geometry(result.find(first)).bounds.y, 20.f);
    EXPECT_EQ(tree.first_child(root), first);
    EXPECT_EQ(tree.next_sibling(first), favorite);
    EXPECT_EQ(tree.next_sibling(favorite), second_favorite);
    EXPECT_EQ(tree.parent(second_favorite), root);
}

/// @brief Recycled slot numbers cannot replace the physical insertion ordinal as a stable
/// tie-breaker.
TEST_F(stack_state_test, equal_keys_follow_insertion_even_when_public_slots_are_recycled)
{
    const auto root = tree.create();
    tree.set_panel(root, vertical_layout{});
    const auto removed = item(root, {10, 10});
    const auto earlier = item(root, {10, 20});
    tree.remove(removed);
    const auto later = item(root, {10, 30});
    ASSERT_LT(later.index, earlier.index);
    tree.set_stack_slot(earlier, {-1, 1, 1});
    tree.set_stack_slot(later, {-1, 1, 1});
    const auto result = engine.update(tree, {0, 0, 100, 100});
    EXPECT_EQ(require_geometry(result.find(earlier)).bounds.y, 0.f);
    EXPECT_EQ(require_geometry(result.find(later)).bounds.y, 20.f);
}

/// @brief Sparse overrides survive compatible axis changes and reset on a new parent relationship.
TEST_F(stack_state_test, slots_reset_on_reparent_or_incompatible_conversion)
{
    const auto root = tree.create();
    const auto other = tree.create();
    tree.set_panel(root, vertical_layout{});
    tree.set_panel(other, horizontal_layout{});
    const auto child = item(root, {10, 10});
    tree.set_stack_slot(child, {-5, 2, 0});
    const auto snapshot = tree.snapshot();
    tree.set_panel(root, horizontal_layout{});
    EXPECT_EQ(tree.stack_slot_for(child), (std::optional{stack_slot{-5, 2, 0}}));
    tree.reparent(child, other);
    EXPECT_EQ(tree.stack_slot_for(child), std::optional{stack_slot{}});
    EXPECT_TRUE(tree.snapshot().stack_slots().empty());
    tree.set_stack_slot(child, {-1, 1, 1});
    tree.set_panel(other, canvas_layout{});
    EXPECT_FALSE(tree.stack_slot_for(child));
    EXPECT_EQ(tree.canvas_slot_for(child), std::optional{canvas_slot{}});
    ASSERT_EQ(snapshot.stack_slots().size(), 1U);
    EXPECT_EQ(snapshot.stack_slots()[0].slot.order, -5);
}

/// @brief Child panels receive allocated bounds before arranging their own independent axes.
TEST_F(stack_state_test, nested_stacks_use_resolved_parent_extents)
{
    const auto root = tree.create();
    tree.set_panel(root, vertical_layout{0, stack_distribution::start, stack_fill::weighted});
    const auto row = tree.create(root);
    tree.set_panel(row, horizontal_layout{0, stack_distribution::start, stack_fill::weighted});
    const auto left = item(row, {20, 10});
    const auto right = item(row, {30, 10});
    const auto below = item(root, {10, 20});
    const auto result = engine.update(tree, {0, 0, 100, 100});
    EXPECT_EQ(require_geometry(result.find(row)).bounds, (layout_rect{0, 0, 100, 45}));
    EXPECT_EQ(require_geometry(result.find(left)).bounds, (layout_rect{0, 0, 45, 45}));
    EXPECT_EQ(require_geometry(result.find(right)).bounds, (layout_rect{45, 0, 55, 45}));
    EXPECT_EQ(require_geometry(result.find(below)).bounds, (layout_rect{0, 45, 100, 55}));
}

/// @brief Reordering one root invalidates its geometry without recomputing another independent
/// root.
TEST_F(stack_state_test, order_changes_reject_old_publication_and_reuse_unaffected_roots)
{
    const auto root = tree.create();
    tree.set_panel(root, vertical_layout{});
    const auto first = item(root, {10, 10});
    const auto last = item(root, {10, 20});
    const auto other = tree.create();
    const auto previous = engine.update(tree, {0, 0, 100, 100});
    tree.acknowledge(other);
    tree.set_stack_slot(last, {-1, 1, 1});
    EXPECT_FALSE(tree.computed(first));
    EXPECT_THROW(tree.publish_layout(previous), std::invalid_argument);
    const auto result = engine.update(tree, {0, 0, 100, 100});
    EXPECT_EQ(require_geometry(result.find(first)).bounds.y, 20.f);
    EXPECT_EQ(engine.statistics().cached_roots, 1U);
    EXPECT_FALSE(tree.dirty(other).paint);
}
} // namespace atlas::ui::tests
