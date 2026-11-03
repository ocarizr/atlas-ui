/** @file @brief Parent-owned placement, visual origins and safe container transitions. */
#include "layout_test_support.hpp"
#include <atlas/jobs/job_system.hpp>
#include <atlas/ui/element_tree.hpp>
#include <atlas/ui/layout_engine.hpp>
#include <gtest/gtest.h>

namespace atlas::ui::tests
{
/// @brief Panels own placement policy while each fact owns all mutable execution state.
class canvas_state_test : public ::testing::Test
{
  protected:
    jobs::job_system pool{2};                ///< Persistent shared pool for the complete fact.
    element_tree tree;                       ///< No native window or renderer is required.
    layout_engine engine{pool.dispatcher()}; ///< Inject the same dispatcher.
};

/// @brief Canvas-specific metadata appears only when the parent supplies that contract.
TEST_F(canvas_state_test, slots_follow_parent_kind_and_reparenting_resets_overrides)
{
    const auto canvas = tree.create();
    const auto flow = tree.create();
    const auto child = tree.create(canvas);
    EXPECT_FALSE(tree.canvas_slot_for(child));
    tree.set_panel(canvas, canvas_layout{});
    EXPECT_EQ(tree.canvas_slot_for(child), std::optional{canvas_slot{}});
    canvas_slot slot;
    slot.horizontal.anchor_min = slot.horizontal.anchor_max = 0.5f;
    tree.set_canvas_slot(child, slot);
    EXPECT_EQ(tree.canvas_slot_for(child), std::optional{slot});
    tree.reparent(child, flow);
    EXPECT_FALSE(tree.canvas_slot_for(child));
    EXPECT_TRUE(tree.snapshot().canvas_slots().empty());
    EXPECT_THROW(tree.set_canvas_slot(child, slot), std::invalid_argument);
    tree.reparent(child, canvas);
    EXPECT_EQ(tree.canvas_slot_for(child), std::optional{canvas_slot{}});
}

/// @brief Normalized point anchors and placement pivots are independent from visual origins.
TEST_F(canvas_state_test, point_anchors_keep_child_centered_after_resize)
{
    const auto canvas = tree.create();
    tree.set_panel(canvas, canvas_layout{});
    const auto child = tree.create(canvas);
    layout_style style;
    style.intrinsic = {20, 10};
    tree.set_layout(child, style);
    canvas_slot slot;
    slot.horizontal = {0.5f, 0.5f, 0, 0, 0.5f};
    slot.vertical = {0.5f, 0.5f, 0, 0, 0.5f};
    tree.set_canvas_slot(child, slot);
    const auto first = engine.update(tree, {0, 0, 200, 100});
    EXPECT_EQ(require_geometry(first.find(child)).bounds, (layout_rect{90, 45, 20, 10}));
    const auto second = engine.update(tree, {10, 20, 100, 200});
    EXPECT_EQ(require_geometry(second.find(child)).bounds, (layout_rect{50, 115, 20, 10}));
    EXPECT_EQ(require_geometry(first.find(child)).bounds, (layout_rect{90, 45, 20, 10}));
}

/// @brief Paint-only transform origin changes do not invalidate numeric layout/cache identity.
TEST_F(canvas_state_test, visual_origin_is_general_and_separate_from_canvas_placement)
{
    const auto root = tree.create();
    engine.update(tree, {10, 20, 100, 50});
    const auto revision = tree.snapshot().revision();
    tree.acknowledge(root);
    tree.set_visual(root, {{1, 0}});
    EXPECT_EQ(tree.snapshot().revision(), revision);
    EXPECT_FALSE(tree.dirty(root).layout);
    EXPECT_TRUE(tree.dirty(root).paint);
    const auto bounds = require_geometry(tree.computed(root)).bounds;
    EXPECT_EQ(resolve_transform_origin(bounds, tree.visual(root)), (layout_point{110, 20}));
    engine.update(tree, {10, 20, 100, 50});
    EXPECT_EQ(engine.statistics().measured_nodes, 0U);
    EXPECT_EQ(tree.snapshot().elements()[0].visual, (visual_style{{1, 0}}));
}

/// @brief Slot changes participate in exact root reuse and retained captures remain immutable.
TEST_F(canvas_state_test, slot_change_invalidates_cache_and_old_publication)
{
    const auto canvas = tree.create();
    tree.set_panel(canvas, canvas_layout{});
    const auto child = tree.create(canvas);
    tree.create();
    canvas_slot slot;
    slot.horizontal.offset_min = 10;
    tree.set_canvas_slot(child, slot);
    const auto input = tree.snapshot();
    const auto first = engine.update(tree, {0, 0, 100, 50});
    slot.horizontal.offset_min = 20;
    tree.set_canvas_slot(child, slot);
    EXPECT_FALSE(tree.computed(child));
    EXPECT_THROW(tree.publish_layout(first), std::invalid_argument);
    const auto next = engine.update(tree, {0, 0, 100, 50});
    EXPECT_EQ(engine.statistics().cached_roots, 1U);
    EXPECT_EQ(require_geometry(next.find(child)).bounds.x, 20.f);
    EXPECT_EQ(require_geometry(first.find(child)).bounds.x, 10.f);
    ASSERT_EQ(input.canvas_slots().size(), 1U);
    EXPECT_EQ(input.canvas_slots()[0].slot.horizontal.offset_min, 10.f);
    tree.set_panel(canvas, overlay_layout{});
    EXPECT_FALSE(tree.canvas_slot_for(child));
    EXPECT_TRUE(tree.snapshot().canvas_slots().empty());
}
} // namespace atlas::ui::tests
