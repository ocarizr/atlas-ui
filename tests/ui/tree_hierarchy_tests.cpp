/** @file @brief Identity, topology validation and safe recursive removal. */
#include "tree_test_support.hpp"
#include <future>

namespace atlas::ui::tests
{
/// @brief Hierarchy changes reject cycles and leave both structure and dirtiness untouched.
TEST_F(tree_test, rejects_cycles_without_mutation)
{
    EXPECT_EQ(tree.parent(child), root);
    EXPECT_THROW(tree.reparent(root, child), std::invalid_argument);
    EXPECT_THROW(tree.reparent(root, root), std::invalid_argument);
    EXPECT_EQ(tree.parent(root), element_handle{});
    EXPECT_EQ(tree.parent(child), root);
    EXPECT_FALSE(tree.dirty(root).layout);
}

/// @brief Moving a branch marks affected ancestors but does not touch unrelated roots.
TEST_F(tree_test, reparent_invalidates_old_and_new_ancestry)
{
    const auto unrelated = tree.create();
    tree.acknowledge(unrelated);
    tree.reparent(child, other);
    EXPECT_EQ(tree.parent(child), other);
    EXPECT_TRUE(tree.dirty(root).layout);
    EXPECT_TRUE(tree.dirty(other).layout);
    EXPECT_TRUE(tree.dirty(child).paint);
    EXPECT_FALSE(tree.dirty(unrelated).layout);
    tree.acknowledge(child);
    tree.reparent(child, other);
    EXPECT_FALSE(tree.dirty(child).paint);
}

/// @brief Removing a subtree invalidates all descendants and reuses slots with new generations.
TEST_F(tree_test, subtree_removal_never_resurrects_a_stale_handle)
{
    const auto leaf = tree.create(child);
    tree.remove(child);
    EXPECT_FALSE(tree.contains(child));
    EXPECT_FALSE(tree.contains(leaf));
    EXPECT_EQ(tree.size(), 2U);
    EXPECT_TRUE(tree.dirty(root).layout);
    EXPECT_FALSE(tree.dirty(other).layout);
    const auto replacement = tree.create(root);
    EXPECT_EQ(replacement.index, leaf.index);
    EXPECT_NE(replacement.generation, leaf.generation);
    EXPECT_NE(replacement, child);
    EXPECT_FALSE(tree.contains(child));
    EXPECT_FALSE(tree.contains(leaf));
    EXPECT_THROW(tree.remove(child), std::invalid_argument);
    EXPECT_THROW(tree.create(child), std::invalid_argument);
    EXPECT_THROW(tree.reparent(replacement, leaf), std::invalid_argument);
}

/// @brief Session identity distinguishes equal local slot/generation values.
TEST_F(tree_test, foreign_handles_are_rejected_before_topology_changes)
{
    element_tree foreign;
    const auto target = foreign.create();
    EXPECT_EQ(target.index, root.index);
    EXPECT_EQ(target.generation, root.generation);
    EXPECT_NE(target.session, root.session);
    EXPECT_FALSE(tree.contains(target));
    EXPECT_THROW(tree.create(target), std::invalid_argument);
    EXPECT_THROW(tree.reparent(child, target), std::invalid_argument);
    EXPECT_EQ(tree.parent(child), root);
}

/// @brief Capacity rejection preserves live elements and permits admission after removal.
TEST(element_tree_limits, capacity_is_explicit_and_reusable)
{
    element_tree tree{{2}};
    const auto first = tree.create();
    const auto second = tree.create(first);
    EXPECT_THROW(tree.create(), std::overflow_error);
    EXPECT_EQ(tree.size(), 2U);
    tree.remove(first);
    EXPECT_EQ(tree.size(), 0U);
    EXPECT_FALSE(tree.contains(second));
    EXPECT_TRUE(tree.contains(tree.create()));
    EXPECT_THROW((element_tree{{0}}), std::invalid_argument);
}

/// @brief Insertion and removal preserve both ends of sibling lists and allow detaching roots.
TEST_F(tree_test, sibling_links_survive_middle_first_and_last_removal)
{
    const auto middle = tree.create(root);
    const auto last = tree.create(root);
    EXPECT_EQ(tree.first_child(root), child);
    EXPECT_EQ(tree.next_sibling(child), middle);
    tree.remove(middle);
    EXPECT_EQ(tree.next_sibling(child), last);
    tree.remove(child);
    EXPECT_EQ(tree.first_child(root), last);
    tree.reparent(last);
    EXPECT_EQ(tree.first_child(root), element_handle{});
    EXPECT_EQ(tree.parent(last), element_handle{});
    tree.reparent(last, root);
    EXPECT_EQ(tree.first_child(root), last);
    tree.remove(last);
    EXPECT_EQ(tree.first_child(root), element_handle{});
}

/// @brief Deep hierarchy destruction and invalidation do not consume recursive call-stack space.
TEST(element_tree_hierarchy, deep_subtrees_are_removed_iteratively)
{
    element_tree tree{{1024}};
    const auto root = tree.create();
    auto parent = root;
    for (std::size_t index = 1; index < 1024; ++index)
    {
        parent = tree.create(parent);
    }

    tree.remove(root);
    EXPECT_EQ(tree.size(), 0U);
    EXPECT_FALSE(tree.contains(parent));
}

/// @brief Foreign-thread operations fail before reading or writing mutable registry state.
TEST_F(tree_test, foreign_access_is_rejected_without_state_changes)
{
    auto result = std::async(std::launch::async,
                             [this]
                             {
                                 EXPECT_THROW(tree.create(), std::logic_error);
                                 EXPECT_THROW(tree.contains(root), std::logic_error);
                                 EXPECT_THROW(tree.requested(root), std::logic_error);
                                 EXPECT_THROW(tree.commands(), std::logic_error);
                             });
    result.get();
    EXPECT_EQ(tree.size(), 3U);
    EXPECT_FALSE(tree.dirty(root).layout);
}

/// @brief Invalid reads and acknowledgment cannot silently accept an empty identity.
TEST_F(tree_test, invalid_observations_fail_explicitly)
{
    EXPECT_THROW(tree.parent({}), std::invalid_argument);
    EXPECT_THROW(tree.first_child({}), std::invalid_argument);
    EXPECT_THROW(tree.next_sibling({}), std::invalid_argument);
    EXPECT_THROW(tree.requested({}), std::invalid_argument);
    EXPECT_THROW(tree.dirty({}), std::invalid_argument);
    EXPECT_THROW(tree.acknowledge({}), std::invalid_argument);
    EXPECT_THROW((element_tree{{65537}}), std::invalid_argument);
}
} // namespace atlas::ui::tests
