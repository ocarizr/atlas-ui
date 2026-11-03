/** @file @brief Owned snapshots isolate worker inputs from mutable live tree state. */
#include "tree_test_support.hpp"

namespace atlas::ui::tests
{
/// @brief Captured topology/properties survive mutation and destruction of the original session.
TEST(tree_snapshots, snapshots_retain_values_and_do_not_borrow_the_registry)
{
    const element_snapshot empty;
    EXPECT_EQ(empty.session(), 0U);
    EXPECT_TRUE(empty.elements().empty());
    const auto capture = []
    {
        element_tree tree;
        const auto root = tree.create();
        tree.create(root);
        auto snapshot = tree.snapshot();
        tree.remove(root);
        EXPECT_EQ(tree.size(), 0U);
        return snapshot;
    };
    const auto snapshot = capture();
    ASSERT_EQ(snapshot.elements().size(), 2U);
    EXPECT_EQ(snapshot.session(), snapshot.elements()[0].target.session);
    EXPECT_EQ(snapshot.elements()[0].first_child, snapshot.elements()[1].target);
    EXPECT_EQ(snapshot.elements()[1].parent, snapshot.elements()[0].target);
    EXPECT_EQ(snapshot.elements()[0].requested.width, 0.f);
}

/// @brief A stable input snapshot does not change when later command batches are applied.
TEST_F(tree_test, requested_changes_do_not_retroactively_modify_a_snapshot)
{
    const auto before = tree.snapshot();
    tree.commands().begin_frame(1);
    tree.commands().set(root, property::width, 10.f, 1);
    tree.apply(tree.commands().seal());
    const auto after = tree.snapshot();
    EXPECT_EQ(before.elements()[0].requested.width, 0.f);
    EXPECT_EQ(after.elements()[0].requested.width, 10.f);
    EXPECT_FALSE(before.elements()[0].dirty.layout);
    EXPECT_TRUE(after.elements()[0].dirty.layout);
}
} // namespace atlas::ui::tests
