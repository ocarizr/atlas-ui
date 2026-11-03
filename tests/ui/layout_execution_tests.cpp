/** @file @brief Joined layout, cache safety and finite numerical failure across both paths. */
#include "layout_test_support.hpp"
#include <atlas/jobs/job_system.hpp>
#include <atlas/ui/element_tree.hpp>
#include <atlas/ui/layout_engine.hpp>
#include <gtest/gtest.h>
#include <limits>
#include <thread>

namespace atlas::ui::tests
{
/// @brief Both execution paths use identical iterative algorithms and stable branch reduction.
TEST(layout_execution, sequential_and_parallel_forests_are_identical)
{
    jobs::job_system pool{4};
    element_tree tree;
    for (int root_index = 0; root_index < 16; ++root_index)
    {
        const auto root = tree.create();
        layout_style row;
        row.kind = layout_kind::horizontal;
        row.spacing = 3;
        tree.set_layout(root, row);
        for (int child_index = 0; child_index < 4; ++child_index)
        {
            const auto child = tree.create(root);
            layout_style style;
            style.intrinsic = {static_cast<float>(child_index + 1),
                               static_cast<float>(root_index + 1)};
            tree.set_layout(child, style);
        }
    }

    tree.set_viewport({10, 20, 320, 180});
    const auto input = tree.snapshot();
    layout_engine sequential{pool.dispatcher(),
                             {65536, std::numeric_limits<std::size_t>::max(), 4}};
    layout_engine parallel{pool.dispatcher(), {65536, 1, 4}};
    const auto reference = sequential.calculate(input);
    const auto result = parallel.calculate(input);
    ASSERT_EQ(reference.items().size(), result.items().size());
    EXPECT_TRUE(
        std::equal(reference.items().begin(), reference.items().end(), result.items().begin()));
    EXPECT_EQ(sequential.statistics().admitted_jobs, 0U);
    EXPECT_EQ(parallel.statistics().admitted_jobs, 10U);
}

/// @brief Deep ownership is not implemented as recursive measure or arrange calls.
TEST(layout_execution, deep_tree_has_no_depth_dependent_phase_or_stack_limit)
{
    jobs::job_system pool{1};
    element_tree tree{{1024}};
    const auto root = tree.create();
    auto leaf = root;
    for (int index = 1; index < 1024; ++index)
    {
        leaf = tree.create(leaf);
    }

    layout_engine engine{pool.dispatcher()};
    const auto result = engine.update(tree, {0, 0, 100, 50});
    EXPECT_EQ(result.items().size(), 1024U);
    EXPECT_EQ(require_geometry(result.find(leaf)).bounds, (layout_rect{0, 0, 100, 50}));
    EXPECT_EQ(engine.statistics().admitted_jobs, 0U);
}

/// @brief An overflow in a worker phase joins accepted work and leaves the engine retryable.
TEST(layout_execution, numerical_failure_does_not_poison_future_calculations)
{
    jobs::job_system pool{2};
    element_tree tree;
    std::vector<element_handle> children;
    for (int index = 0; index < 2; ++index)
    {
        const auto root = tree.create();
        layout_style row;
        row.kind = layout_kind::horizontal;
        tree.set_layout(root, row);
        for (int child_index = 0; child_index < 2; ++child_index)
        {
            const auto child = tree.create(root);
            layout_style huge;
            huge.intrinsic.width = std::numeric_limits<float>::max();
            tree.set_layout(child, huge);
            children.push_back(child);
        }
    }

    tree.set_viewport({0, 0, 100, 50});
    layout_engine engine{pool.dispatcher(), {65536, 1, 2}};
    EXPECT_THROW(engine.calculate(tree.snapshot()), std::overflow_error);
    EXPECT_TRUE(engine.statistics().failed);
    for (const auto child : children)
    {
        tree.set_layout(child, layout_style{});
    }

    const auto result = engine.calculate(tree.snapshot());
    EXPECT_EQ(result.items().size(), 6U);
    EXPECT_FALSE(engine.statistics().failed);
}

/// @brief Prior cancellation does not even change the requested viewport or old geometry.
TEST(layout_execution, prior_cancellation_rejects_before_live_mutation)
{
    jobs::job_system pool{1};
    element_tree tree;
    const auto root = tree.create();
    layout_engine engine{pool.dispatcher()};
    engine.update(tree, {0, 0, 100, 50});
    const auto before = tree.snapshot();
    std::stop_source stop;
    stop.request_stop();
    EXPECT_THROW(
        engine.update(tree, {0, 0, 50, 100}, {jobs::job_priority::normal, stop.get_token()}),
        jobs::job_cancelled);
    EXPECT_EQ(tree.snapshot().revision(), before.revision());
    EXPECT_EQ(require_geometry(tree.computed(root)).bounds.width, 100.f);
    EXPECT_EQ(engine.statistics().admitted_jobs, 0U);
}

/// @brief Empty/default packets, node budgets and coordinator affinity are explicit contracts.
TEST(layout_execution, validates_input_budget_and_owner)
{
    jobs::job_system pool{1};
    element_tree tree;
    layout_engine engine{pool.dispatcher(), {1}};
    EXPECT_THROW(engine.calculate({}), std::invalid_argument);
    EXPECT_TRUE(engine.calculate(tree.snapshot()).items().empty());
    tree.create();
    tree.create();
    EXPECT_THROW(engine.calculate(tree.snapshot()), std::overflow_error);
    std::jthread foreign{[&]
                         {
                             EXPECT_THROW(engine.statistics(), std::logic_error);
                         }};
    foreign.join();
    EXPECT_THROW((layout_engine{pool.dispatcher(), {0}}), std::invalid_argument);
}
} // namespace atlas::ui::tests
