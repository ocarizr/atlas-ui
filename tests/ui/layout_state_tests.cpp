/** @file @brief Requested/computed separation, invalidation, stale publication and cache isolation.
 */
#include "layout_test_support.hpp"
#include <atlas/jobs/job_system.hpp>
#include <atlas/ui/element_tree.hpp>
#include <atlas/ui/layout_engine.hpp>
#include <gtest/gtest.h>

namespace atlas::ui::tests
{
/// @brief Width zero remains a real fixed request; monostate restores automatic sizing.
TEST(layout_state, fixed_zero_and_auto_are_distinct)
{
    jobs::job_system pool{1};
    element_tree tree;
    const auto root = tree.create();
    layout_engine engine{pool.dispatcher()};
    tree.commands().begin_frame(1);
    tree.commands().set(root, property::width, 0.f, 1);
    tree.apply(tree.commands().seal());
    engine.update(tree, {0, 0, 100, 50});
    ASSERT_TRUE(tree.computed(root));
    EXPECT_EQ(require_geometry(tree.computed(root)).bounds.width, 0.f);
    tree.commands().set(root, property::width, std::monostate{}, 1);
    tree.apply(tree.commands().seal());
    EXPECT_FALSE(tree.computed(root));
    engine.update(tree, {0, 0, 100, 50});
    EXPECT_EQ(require_geometry(tree.computed(root)).bounds.width, 100.f);
}

/// @brief No computed value is changed when an old input revision or foreign session is published.
TEST(layout_state, publication_rejects_stale_geometry_atomically)
{
    jobs::job_system pool{1};
    element_tree tree;
    const auto root = tree.create();
    layout_engine engine{pool.dispatcher()};
    const auto previous = engine.update(tree, {0, 0, 100, 50});
    tree.set_viewport({0, 0, 50, 100});
    EXPECT_FALSE(tree.computed(root));
    EXPECT_THROW(tree.publish_layout(previous), std::invalid_argument);
    EXPECT_FALSE(tree.computed(root));
    const auto next = engine.calculate(tree.snapshot());
    tree.publish_layout(next);
    EXPECT_EQ(require_geometry(tree.computed(root)).bounds.height, 100.f);
    element_tree foreign;
    foreign.create();
    EXPECT_THROW(foreign.publish_layout(next), std::invalid_argument);
    EXPECT_FALSE(foreign.computed(foreign.snapshot().elements()[0].target));
}

/// @brief Changing one independent root avoids recalculating an unaffected branch.
TEST(layout_state, cache_reuses_unchanged_roots_and_respects_invalidation)
{
    jobs::job_system pool{2};
    element_tree tree;
    const auto first = tree.create();
    const auto child = tree.create(first);
    const auto other = tree.create();
    layout_engine engine{pool.dispatcher()};
    engine.update(tree, {0, 0, 100, 50});
    tree.acknowledge(other);
    tree.commands().begin_frame(1);
    tree.commands().set(child, property::width, 10.f, 1);
    tree.apply(tree.commands().seal());
    EXPECT_FALSE(tree.computed(first));
    EXPECT_TRUE(tree.computed(other));
    engine.update(tree, {0, 0, 100, 50});
    EXPECT_EQ(engine.statistics().cached_roots, 1U);
    EXPECT_EQ(engine.statistics().measured_nodes, 2U);
    EXPECT_FALSE(tree.dirty(other).paint);
    engine.update(tree, {0, 0, 100, 50});
    EXPECT_EQ(engine.statistics().measured_nodes, 0U);
}

/// @brief An invalidated ancestor prevents stale sibling reads even after manual dirty
/// acknowledgment.
TEST(layout_state, stack_siblings_follow_ancestor_invalidation)
{
    jobs::job_system pool{1};
    element_tree tree;
    const auto root = tree.create();
    layout_style row;
    row.kind = layout_kind::horizontal;
    tree.set_layout(root, row);
    const auto first = tree.create(root);
    const auto second = tree.create(root);
    tree.commands().begin_frame(1);
    tree.commands().set(first, property::width, 10.f, 1);
    tree.apply(tree.commands().seal());
    layout_engine engine{pool.dispatcher()};
    engine.update(tree, {0, 0, 100, 50});
    EXPECT_EQ(require_geometry(tree.computed(second)).bounds.x, 10.f);
    tree.commands().set(first, property::width, 20.f, 1);
    tree.apply(tree.commands().seal());
    tree.acknowledge(root);
    EXPECT_FALSE(tree.computed(second));
    engine.update(tree, {0, 0, 100, 50});
    EXPECT_EQ(require_geometry(tree.computed(second)).bounds.x, 20.f);
    const auto revision = tree.snapshot().revision();
    tree.commands().set(first, property::visible, false, 1);
    tree.apply(tree.commands().seal());
    engine.update(tree, {0, 0, 100, 50});
    EXPECT_EQ(tree.snapshot().revision(), revision);
    EXPECT_EQ(engine.statistics().measured_nodes, 0U);
    EXPECT_EQ(require_geometry(tree.computed(second)).bounds.x, 20.f);
}
} // namespace atlas::ui::tests
