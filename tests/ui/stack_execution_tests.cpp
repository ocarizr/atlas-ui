/** @file @brief Seeded independent references and exact sequential/parallel geometry publication.
 */
#include "layout_test_support.hpp"
#include "stack_reference_support.hpp"
#include <atlas/jobs/job_system.hpp>
#include <atlas/ui/element_tree.hpp>
#include <atlas/ui/layout_engine.hpp>
#include <gtest/gtest.h>
#include <numeric>
#include <random>

namespace atlas::ui::tests
{
/// @brief The engine seed and modulo mapping are explicit and independent from worker scheduling.
stack_reference scenario(std::mt19937& random)
{
    stack_reference data{{}, static_cast<float>(random() % 201), static_cast<float>(random() % 4)};
    data.children.reserve(8);
    for (int index = 0; index < 8; ++index)
    {
        const auto size = static_cast<float>(1 + random() % 30);
        data.children.push_back({size, static_cast<float>(random() % 5),
                                 size + static_cast<float>(random() % 40),
                                 static_cast<float>(random() % 4), static_cast<float>(random() % 4),
                                 static_cast<float>(random() % 3), static_cast<float>(random() % 3),
                                 static_cast<std::int32_t>(random() % 5) - 2, random() % 5 == 0});
        data.children.back().minimum = std::min(data.children.back().minimum, size);
    }

    return data;
}

/// @brief All trees share the application pool; no worker accesses a mutable tree or reference
/// model.
TEST(stack_execution, seeded_models_match_and_worker_counts_do_not_change_geometry)
{
    jobs::job_system pool{4};
    layout_engine serial{pool.dispatcher(), {64, 1, 1}};
    layout_engine parallel{pool.dispatcher(), {64, 1, 4}};
    std::mt19937 random{0x112u};
    for (int iteration = 0; iteration < 64; ++iteration)
    {
        SCOPED_TRACE(iteration);
        const auto data = scenario(random);
        const auto expected = reference_sizes(data);
        std::vector<std::size_t> order(data.children.size());
        std::iota(order.begin(), order.end(), 0);
        std::stable_sort(order.begin(), order.end(),
                         [&](auto left, auto right)
                         {
                             return data.children[left].order < data.children[right].order;
                         });
        element_tree tree{{64}};
        std::vector<element_handle> first_branch;
        first_branch.reserve(data.children.size());
        tree.commands().begin_frame(1);
        for (int branch = 0; branch < 4; ++branch)
        {
            const auto root = tree.create();
            tree.set_panel(root, horizontal_layout{data.gap, stack_distribution::start,
                                                   stack_fill::weighted, stack_overflow::shrink});
            for (const auto& child : data.children)
            {
                const auto target = tree.create(root);
                layout_style style;
                style.intrinsic = {child.size, 10};
                style.constraints = {{child.minimum, 0}, {child.maximum, 100}};
                style.margin = {child.before, 0, child.after, 0};
                tree.set_layout(target, style);
                tree.set_stack_slot(target, {child.order, child.grow, child.shrink});
                if (child.fixed)
                {
                    tree.commands().set(target, property::width, child.size, 1);
                }

                if (branch == 0)
                {
                    first_branch.push_back(target);
                }
            }
        }

        tree.apply(tree.commands().seal());
        tree.set_viewport({0, 0, data.extent, 100});
        const auto snapshot = tree.snapshot();
        const auto result = parallel.calculate(snapshot);
        const auto sequential = serial.calculate(snapshot);
        ASSERT_EQ(result.items().size(), sequential.items().size());
        EXPECT_TRUE(
            std::equal(result.items().begin(), result.items().end(), sequential.items().begin()));
        double cursor = 0;
        for (const auto index : order)
        {
            const auto bounds = require_geometry(result.find(first_branch[index])).bounds;
            EXPECT_NEAR(bounds.width, expected[index], 1e-4);
            EXPECT_NEAR(bounds.x, cursor + data.children[index].before, 1e-4);
            cursor += expected[index] + data.children[index].before + data.children[index].after +
                      data.gap;
        }

        EXPECT_EQ(serial.statistics().admitted_jobs, 0U);
        EXPECT_GT(parallel.statistics().admitted_jobs, 0U);
    }
}
} // namespace atlas::ui::tests
