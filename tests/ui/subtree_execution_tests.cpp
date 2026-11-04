/** @file @brief Bounded shared-pool measurement inside one tree preserves the serial reference. */
#include "layout_test_support.hpp"
#include "subtree_test_support.hpp"
#include <atlas/jobs/job_system.hpp>
#include <atlas/ui/element_tree.hpp>
#include <atlas/ui/layout_engine.hpp>
#include <gtest/gtest.h>
#include <random>

namespace atlas::ui::tests
{
/// @brief One canvas with nested panels must no longer require multiple independent roots.
TEST(subtree_execution, one_canvas_splits_nested_panels_and_matches_serial_geometry)
{
    jobs::job_system pool{4};
    element_tree tree{{4096}};
    const auto root = tree.create();
    tree.set_panel(root, canvas_layout{});
    const auto wrapper = tree.create(root);
    tree.set_panel(wrapper, horizontal_layout{3});
    for (int panel = 0; panel < 8; ++panel)
    {
        const auto column = tree.create(wrapper);
        tree.set_panel(column, vertical_layout{2});
        for (int index = 0; index < 16; ++index)
        {
            const auto child = tree.create(column);
            layout_style style;
            style.intrinsic = {static_cast<float>(index + 1), static_cast<float>(panel + 1)};
            tree.set_layout(child, style);
        }
    }

    tree.set_viewport({0, 0, 1280, 720});
    const auto input = tree.snapshot();
    layout_engine serial{pool.dispatcher(), {4096, 1, 1, 1}};
    layout_engine parallel{pool.dispatcher(), {4096, 1, 4, 1}};
    const auto reference = serial.calculate(input);
    const auto result = parallel.calculate(input);
    ASSERT_EQ(result.items().size(), reference.items().size());
    EXPECT_TRUE(
        std::equal(result.items().begin(), result.items().end(), reference.items().begin()));
    EXPECT_EQ(serial.statistics().admitted_jobs, 0U);
    EXPECT_EQ(parallel.statistics().subtree_ranges, 4U);
    EXPECT_EQ(parallel.statistics().admitted_jobs, 5U);
    EXPECT_GT(parallel.statistics().subtree_nodes, 0U);
    EXPECT_GT(parallel.statistics().scheduling_bytes, 0U);
}

/// @brief Cutoff, fan-out and worker counts change execution only, never geometry.
TEST(subtree_execution, seeded_related_trees_match_across_grains_and_worker_counts)
{
    std::mt19937 random{0x114u};
    for (const auto workers : {1U, 2U, 4U})
    {
        jobs::job_system pool{workers};
        for (int iteration = 0; iteration < 12; ++iteration)
        {
            SCOPED_TRACE(iteration);
            SCOPED_TRACE(workers);
            element_tree tree{{2048}};
            const auto root = append_subtree_scenario(tree, {8, 8 + random() % 24});
            auto previous = tree.first_child(root);
            for (auto child = tree.next_sibling(previous); child != element_handle{};
                 child = tree.next_sibling(child))
            {
                tree.set_boundaries(child, {{},
                                            boundary_relation{previous, boundary_side::after,
                                                              static_cast<float>(random() % 4),
                                                              boundary_sizing::remaining}});
                previous = child;
            }

            tree.set_viewport({10, 20, 1280, 720});
            const auto input = tree.snapshot();
            layout_engine serial{pool.dispatcher(), {2048, 1, 1, 1}};
            const auto reference = serial.calculate(input);
            for (const auto grain : {1U, 32U, 4096U})
            {
                layout_engine parallel{pool.dispatcher(), {2048, 1, 4, grain}};
                const auto result = parallel.calculate(input);
                ASSERT_EQ(result.items().size(), reference.items().size());
                EXPECT_TRUE(std::equal(result.items().begin(), result.items().end(),
                                       reference.items().begin()));
                EXPECT_LE(parallel.statistics().admitted_jobs, 5U);
                EXPECT_LE(parallel.statistics().subtree_ranges, 4U);
                EXPECT_EQ(parallel.statistics().admitted_jobs == 0, grain == 4096);
                parallel.calculate(input);
                EXPECT_EQ(parallel.statistics().admitted_jobs, 0U);
                EXPECT_EQ(parallel.statistics().scheduling_bytes, 0U);
            }
        }
    }
}

/// @brief Deep chains, leaf-only roots and insufficient work cannot turn into one task per node.
TEST(subtree_execution, unbranched_and_tiny_work_stay_inline)
{
    jobs::job_system pool{1};
    for (const bool chain : {false, true})
    {
        element_tree tree{{2048}};
        const auto root = tree.create();
        auto parent = root;
        for (int index = 0; index < 1024; ++index)
        {
            const auto child = tree.create(parent);
            if (chain)
            {
                parent = child;
            }
        }

        layout_engine engine{pool.dispatcher(), {2048, 1, 4, 1}};
        EXPECT_EQ(engine.update(tree, {0, 0, 100, 100}).items().size(), 1025U);
        EXPECT_EQ(engine.statistics().admitted_jobs, 0U);
        EXPECT_EQ(engine.statistics().subtree_ranges, 0U);
    }

    element_tree tree;
    append_subtree_scenario(tree, {4, 4});
    layout_engine engine{pool.dispatcher()};
    engine.update(tree, {0, 0, 100, 100});
    EXPECT_EQ(engine.statistics().scheduling_bytes, 0U);
    layout_engine disabled{pool.dispatcher(), {4096, 1, 4, 0}};
    disabled.update(tree, {0, 0, 100, 100});
    EXPECT_EQ(disabled.statistics().admitted_jobs, 0U);
    EXPECT_EQ(disabled.statistics().scheduling_bytes, 0U);
}
} // namespace atlas::ui::tests
