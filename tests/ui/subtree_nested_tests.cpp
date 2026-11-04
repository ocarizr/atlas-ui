/** @file @brief Mixed nesting and reordered siblings exercise fragmented dependency cuts. */
#include "subtree_test_support.hpp"
#include <atlas/jobs/job_system.hpp>
#include <atlas/ui/layout_engine.hpp>
#include <gtest/gtest.h>
#include <random>

namespace atlas::ui::tests
{
/// @brief Parents between disjoint selected intervals are reduced only after the complete join.
TEST(subtree_nested, mixed_nested_panels_and_unselected_leaves_match_every_bounded_policy)
{
    jobs::job_system pool{4};
    std::mt19937 random{0x11445u};
    for (int iteration = 0; iteration < 16; ++iteration)
    {
        SCOPED_TRACE(iteration);
        element_tree tree{{4096}};
        const auto root = tree.create();
        tree.set_panel(root, horizontal_layout{2, stack_distribution::center});
        for (int branch = 0; branch < 6; ++branch)
        {
            auto panel = tree.create(root);
            tree.set_stack_slot(panel, {static_cast<std::int32_t>(random() % 5) - 2, 1, 1});
            const auto depth_count = random() % 4;
            for (std::uint32_t depth = 0; depth < depth_count; ++depth)
            {
                panel = tree.create(panel);
            }

            tree.set_panel(panel, vertical_layout{1, stack_distribution::space_around,
                                                  stack_fill::weighted, stack_overflow::shrink});
            for (int sibling = 0; sibling < 5; ++sibling)
            {
                const auto child = tree.create(panel);
                layout_style style;
                style.intrinsic = {static_cast<float>(random() % 11),
                                   static_cast<float>(random() % 7)};
                tree.set_layout(child, style);
                if (random() % 3 != 0)
                {
                    tree.set_panel(child, overlay_layout{});
                    for (int leaf = 0; leaf < 6; ++leaf)
                    {
                        const auto element = tree.create(child);
                        tree.set_layout(element, style);
                    }
                }
            }
        }

        tree.set_viewport({0, 0, 1280, 720});
        const auto input = tree.snapshot();
        layout_engine reference{pool.dispatcher(), {4096, 1, 1, 1}};
        const auto expected = reference.calculate(input);
        for (const auto chunks : {2U, 3U, 8U})
        {
            for (const auto grain : {1U, 7U})
            {
                layout_engine engine{pool.dispatcher(), {4096, 1, chunks, grain}};
                const auto result = engine.calculate(input);
                ASSERT_EQ(result.items().size(), expected.items().size());
                EXPECT_TRUE(std::equal(result.items().begin(), result.items().end(),
                                       expected.items().begin()));
                EXPECT_LE(engine.statistics().admitted_jobs, chunks + 1);
                EXPECT_LE(engine.statistics().subtree_ranges, chunks);
                EXPECT_LE(engine.statistics().subtree_nodes, input.elements().size());
            }
        }
    }

    pool.wait_idle();
    const auto report = pool.sanitizer_report();
    for (const auto& finding : report.diagnostics)
    {
        EXPECT_NE(finding.severity, jobs::diagnostic_severity::error);
    }
}
} // namespace atlas::ui::tests
