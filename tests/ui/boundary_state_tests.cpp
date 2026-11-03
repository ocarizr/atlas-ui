/** @file @brief Sparse dependencies, cache reuse, invalidation and immutable publication contracts.
 */
#include "boundary_test_support.hpp"

namespace atlas::ui::tests
{
/// @brief Source and relationship edits invalidate the domain while unrelated roots stay reusable.
TEST_F(boundary_test, source_changes_and_dependency_replacement_invalidate_publication)
{
    const auto root = canvas();
    const auto source = tree.create(root);
    const auto next_source = tree.create(root);
    const auto target = tree.create(root);
    const auto descendant = tree.create(target);
    const auto independent = tree.create();
    height(source, 20);
    height(next_source, 40);
    tree.set_boundaries(target, after(source));
    const auto first = engine.update(tree, {0, 0, 100, 100});
    const auto retained = tree.snapshot();
    for (const auto item : {root, source, next_source, target, descendant, independent})
    {
        tree.acknowledge(item);
    }

    height(source, 30);
    EXPECT_FALSE(tree.computed(target));
    EXPECT_TRUE(tree.dirty(target).layout);
    EXPECT_TRUE(tree.dirty(descendant).layout);
    EXPECT_FALSE(tree.dirty(independent).layout);
    EXPECT_THROW(tree.publish_layout(first), std::invalid_argument);
    EXPECT_EQ(require_geometry(engine.update(tree, {0, 0, 100, 100}).find(target)).bounds.y, 30.f);
    EXPECT_EQ(engine.statistics().cached_roots, 1U);
    tree.set_boundaries(target, after(next_source));
    EXPECT_EQ(require_geometry(engine.update(tree, {0, 0, 100, 100}).find(target)).bounds.y, 40.f);
    EXPECT_EQ(engine.statistics().cached_roots, 1U);
    const auto revision = tree.snapshot().revision();
    tree.set_boundaries(target, after(next_source));
    EXPECT_EQ(tree.snapshot().revision(), revision);
    EXPECT_EQ(engine.calculate(retained).find(target), first.find(target));
    ASSERT_EQ(retained.boundaries().size(), 1U);
    EXPECT_EQ(retained.boundaries()[0].boundaries, after(source));
    tree.reset_boundaries(target);
    EXPECT_TRUE(tree.snapshot().boundaries().empty());
    EXPECT_EQ(require_geometry(engine.update(tree, {0, 0, 100, 100}).find(target)).bounds.y, 0.f);
}

/// @brief An impossible update must retain the prior cache and expose no partial live rectangles.
TEST_F(boundary_test, overflow_rejection_keeps_previous_cache_and_is_retryable)
{
    const auto root = canvas();
    const auto source = tree.create(root);
    const auto target = tree.create(root);
    height(source, 20);
    height(target, 30);
    boundary_relation relation{source};
    relation.overflow = boundary_overflow::reject;
    const layout_boundaries value{{}, relation};
    tree.set_boundaries(target, value);
    const auto first = engine.update(tree, {0, 0, 100, 100});
    const auto original = tree.snapshot();
    height(source, 90);
    EXPECT_THROW(engine.update(tree, {0, 0, 100, 100}), std::invalid_argument);
    EXPECT_FALSE(tree.computed(root));
    EXPECT_FALSE(tree.computed(source));
    EXPECT_FALSE(tree.computed(target));
    EXPECT_TRUE(engine.statistics().failed);
    EXPECT_EQ(engine.calculate(original).find(target), first.find(target));
    height(source, 40);
    EXPECT_EQ(require_geometry(engine.update(tree, {0, 0, 100, 100}).find(target)).bounds.y, 40.f);
}

/// @brief Chains are directed even when a later-created source supplies an earlier node's edge.
TEST_F(boundary_test, reverse_chain_and_subtree_removal_preserve_resolved_values)
{
    const auto root = canvas();
    const auto last = tree.create(root);
    const auto middle = tree.create(root);
    const auto first = tree.create(root);
    height(first, 10);
    height(middle, 20);
    height(last, 30);
    tree.set_boundaries(last, after(middle));
    tree.set_boundaries(middle, after(first));
    const auto input = tree.snapshot();
    const auto result = engine.update(tree, {0, 0, 100, 100});
    EXPECT_EQ(require_geometry(result.find(middle)).bounds.y, 10.f);
    EXPECT_EQ(require_geometry(result.find(last)).bounds.y, 30.f);
    tree.remove(root);
    EXPECT_TRUE(tree.snapshot().boundaries().empty());
    EXPECT_EQ(engine.calculate(input).find(last), result.find(last));
}

/// @brief Remaining-space allocation need not represent an unused huge desired rectangle first.
TEST_F(boundary_test, remaining_shrinks_huge_desired_without_intermediate_overflow)
{
    const auto root = canvas();
    const auto source = tree.create(root);
    const auto target = tree.create(root);
    height(target, std::numeric_limits<float>::max());
    boundary_relation relation{source};
    relation.sizing = boundary_sizing::remaining;
    const layout_boundaries value{{}, relation};
    tree.set_boundaries(target, value);
    const auto result = engine.update(tree, {0, 1e38f, 100, 1e38f});
    EXPECT_EQ(require_geometry(result.find(target)).bounds, (layout_rect{0, 1e38f, 0, 1e38f}));
}
} // namespace atlas::ui::tests
