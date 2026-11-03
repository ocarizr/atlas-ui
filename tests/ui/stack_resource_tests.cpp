/** @file @brief Ordered/weighted preparation rolls back cache state and retains snapshot resources.
 */
#include "layout_test_support.hpp"
#include "resource_test_support.hpp"
#include <atlas/jobs/job_system.hpp>
#include <atlas/ui/element_tree.hpp>
#include <atlas/ui/layout_engine.hpp>
#include <gtest/gtest.h>

namespace atlas::ui::tests
{
/// @brief Every observed PMR failure in changed-order and fill scratch preparation is retryable.
TEST(stack_resources, allocation_failures_preserve_previous_cache)
{
    jobs::job_system pool{1};
    element_tree tree;
    const auto root = tree.create();
    tree.set_panel(root, vertical_layout{0, stack_distribution::start, stack_fill::weighted});
    const auto first = tree.create(root);
    const auto second = tree.create(root);
    layout_style style;
    style.intrinsic.height = 20;
    tree.set_layout(first, style);
    tree.set_layout(second, style);
    tree.set_viewport({0, 0, 100, 100});
    const auto original = tree.snapshot();
    tree.set_stack_slot(second, {-1, 2, 1});
    const auto changed = tree.snapshot();
    auto counter = std::make_shared<failing_resource>();
    std::size_t allocations = 0;
    {
        layout_engine engine{pool.dispatcher(), {65536, 1, 1}, counter};
        engine.calculate(original);
        const auto before = counter->calls;
        engine.calculate(changed);
        allocations = counter->calls - before;
    }

    for (std::size_t failure = 0; failure < allocations; ++failure)
    {
        SCOPED_TRACE(failure);
        auto memory = std::make_shared<failing_resource>();
        layout_engine engine{pool.dispatcher(), {65536, 1, 1}, memory};
        engine.calculate(original);
        const auto baseline = memory->live_bytes;
        memory->remaining = failure;
        EXPECT_THROW(engine.calculate(changed), std::bad_alloc);
        EXPECT_EQ(memory->live_bytes, baseline);
        EXPECT_EQ(require_geometry(engine.calculate(original).find(second)).bounds.height, 50.f);
        memory->remaining = std::numeric_limits<std::size_t>::max();
        const auto result = engine.calculate(changed);
        EXPECT_EQ(require_geometry(result.find(second)).bounds.height, 60.f);
        EXPECT_EQ(require_geometry(result.find(second)).bounds.y, 0.f);
    }
}

/// @brief Sparse records retain values and their allocator after component removal and tree
/// destruction.
TEST(stack_resources, snapshot_outlives_live_relationship_and_resource_owner)
{
    auto memory = std::make_shared<failing_resource>();
    std::weak_ptr<std::pmr::memory_resource> lifetime = memory;
    element_snapshot retained;
    {
        element_tree tree{{}, memory};
        const auto root = tree.create();
        tree.set_panel(root, vertical_layout{});
        const auto child = tree.create(root);
        tree.set_stack_slot(child, {-2, 3, 0});
        retained = tree.snapshot();
        tree.reset_stack_slot(child);
        tree.remove(root);
        memory.reset();
    }

    ASSERT_FALSE(lifetime.expired());
    ASSERT_EQ(retained.stack_slots().size(), 1U);
    EXPECT_EQ(retained.stack_slots()[0].slot, (stack_slot{-2, 3, 0}));
    retained = {};
    EXPECT_TRUE(lifetime.expired());
}
} // namespace atlas::ui::tests
