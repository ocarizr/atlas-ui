/** @file @brief Atomic scalar publication, ordered batches and isolated invalidation. */
#include "tree_test_support.hpp"
#include <array>

namespace atlas::ui::tests
{
/// @brief Pending writes do not change reads; only successful apply publishes requested values.
TEST_F(tree_test, publication_updates_requested_state_and_affected_subtree)
{
    auto& commands = tree.commands();
    commands.begin_frame(1);
    commands.set(root, property::width, 120.f, 1);
    EXPECT_EQ(tree.requested(root).width, 0.f);
    const auto batch = commands.seal();
    EXPECT_EQ(tree.requested(root).width, 0.f);
    tree.apply(batch);
    EXPECT_EQ(tree.requested(root).width, 120.f);
    EXPECT_TRUE(tree.dirty(root).layout);
    EXPECT_TRUE(tree.dirty(child).layout);
    EXPECT_FALSE(tree.dirty(other).layout);
    EXPECT_FALSE(tree.dirty(other).paint);
    EXPECT_THROW(tree.apply(batch), std::invalid_argument);
}

/// @brief One stale target rejects every write and preserves the applied cursor for retry.
TEST_F(tree_test, a_stale_target_rejects_the_entire_batch)
{
    auto& commands = tree.commands();
    commands.begin_frame(1);
    commands.set(root, property::width, 120.f, 1);
    commands.set(child, property::height, 40.f, 1);
    const auto batch = commands.seal();
    tree.remove(child);
    tree.acknowledge(root);
    EXPECT_THROW(tree.apply(batch), std::invalid_argument);
    EXPECT_EQ(tree.requested(root).width, 0.f);
    EXPECT_FALSE(tree.dirty(root).layout);
    EXPECT_FALSE(tree.dirty(other).paint);
    commands.begin_frame(2);
    commands.set(root, property::height, 5.f, 1);
    tree.apply(commands.seal());
    EXPECT_EQ(tree.requested(root).height, 5.f);
}

/// @brief Invalidating a descendant's size dirties ancestry but not sibling geometry.
TEST_F(tree_test, size_changes_reach_ancestors_while_visual_changes_only_reach_descendants)
{
    const auto sibling = tree.create(root);
    tree.acknowledge(root);
    tree.acknowledge(sibling);
    auto& commands = tree.commands();
    commands.begin_frame(1);
    commands.set(child, property::height, 10.f, 1);
    tree.apply(commands.seal());
    EXPECT_TRUE(tree.dirty(root).layout);
    EXPECT_FALSE(tree.dirty(sibling).layout);
    tree.acknowledge(root);
    tree.acknowledge(child);
    commands.set(root, property::visible, false, 1);
    tree.apply(commands.seal());
    EXPECT_FALSE(tree.requested(root).visible);
    EXPECT_TRUE(tree.requested(child).visible);
    EXPECT_TRUE(tree.dirty(child).paint);
    EXPECT_FALSE(tree.dirty(child).layout);
    EXPECT_FALSE(tree.dirty(other).paint);
}

/// @brief Batch session identity is checked even for an empty packet.
TEST_F(tree_test, foreign_and_unbound_batches_cannot_mutate_the_tree)
{
    element_tree foreign;
    foreign.commands().begin_frame(1);
    EXPECT_THROW(tree.apply(foreign.commands().seal()), std::invalid_argument);
    command_buffer unbound;
    unbound.begin_frame(1);
    unbound.set(root, property::width, 44.f, 1);
    EXPECT_THROW(tree.apply(unbound.seal()), std::invalid_argument);
    tree.commands().begin_frame(1);
    EXPECT_THROW(tree.commands().set(foreign.create(), property::width, 1.f, 1),
                 std::invalid_argument);
    EXPECT_EQ(tree.commands().pending(), 0U);
    EXPECT_EQ(tree.requested(root).width, 0.f);
}

/// @brief Retained history still warns across publication boundaries in the same frame.
TEST_F(tree_test, ordered_application_preserves_frame_wide_conflicts)
{
    auto& commands = tree.commands();
    commands.begin_frame(1);
    commands.set(child, property::opacity, .5f, 1);
    const auto first = commands.seal();
    commands.set(child, property::opacity, .25f, 2);
    const auto second = commands.seal();
    EXPECT_THROW(tree.apply(second), std::invalid_argument);
    tree.apply(first);
    tree.apply(second);
    EXPECT_EQ(tree.requested(child).opacity, .25f);
    EXPECT_FALSE(tree.dirty(child).layout);
    EXPECT_TRUE(tree.dirty(child).paint);
    EXPECT_FALSE(tree.dirty(other).paint);
    ASSERT_EQ(second.conflicts().size(), 1U);
    EXPECT_EQ(second.conflicts()[0].writes_this_frame, 2U);
    commands.begin_frame(2);
    tree.apply(commands.seal());
    EXPECT_THROW(tree.apply(first), std::invalid_argument);
}

/// @brief Repeat assignments with the same committed value add no invalidation.
TEST_F(tree_test, equal_committed_values_are_noops)
{
    auto& commands = tree.commands();
    commands.begin_frame(1);
    commands.set(root, property::visible, true, 1);
    tree.apply(commands.seal());
    EXPECT_FALSE(tree.dirty(root).paint);
    EXPECT_FALSE(tree.dirty(child).paint);
}

/// @brief Serial command merging agrees with a deterministic last-assignment reference.
TEST_F(tree_test, merged_state_matches_serial_reference)
{
    std::array targets{root, child, other};
    std::array<float, 3> expected{};
    auto& commands = tree.commands();
    commands.begin_frame(1);
    for (std::size_t index = 0; index < 100; ++index)
    {
        const auto slot = (index * 7U) % targets.size();
        expected[slot] = static_cast<float>(index);
        commands.set(targets[slot], property::width, expected[slot], 1);
    }

    tree.apply(commands.seal());
    for (std::size_t index = 0; index < targets.size(); ++index)
    {
        EXPECT_EQ(tree.requested(targets[index]).width, expected[index]);
    }
}
} // namespace atlas::ui::tests
