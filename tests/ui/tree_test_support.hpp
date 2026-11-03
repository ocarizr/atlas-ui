/** @file @brief Small owner-thread tree fixtures with independent state per test. */
#pragma once
#include <atlas/ui/element_tree.hpp>
#include <gtest/gtest.h>

namespace atlas::ui::tests
{
/// @brief A root, a child and an unrelated root with acknowledged initial invalidation.
class tree_test : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        root = tree.create();
        child = tree.create(root);
        other = tree.create();
        tree.acknowledge(root);
        tree.acknowledge(child);
        tree.acknowledge(other);
    } ///< Each case starts with the same clean topology.

    element_tree tree;    ///< Owns the session, registry and serial producer.
    element_handle root;  ///< First independent root.
    element_handle child; ///< Child of the first root.
    element_handle other; ///< Unrelated subtree.
};
} // namespace atlas::ui::tests
