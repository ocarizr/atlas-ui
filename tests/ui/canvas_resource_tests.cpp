/** @file @brief Sparse capture ownership, cache rollback and joined canvas equivalence. */
#include "layout_test_support.hpp"
#include "resource_test_support.hpp"
#include <algorithm>
#include <atlas/jobs/job_system.hpp>
#include <atlas/ui/element_tree.hpp>
#include <atlas/ui/layout_engine.hpp>
#include <gtest/gtest.h>

namespace atlas::ui::tests
{
/// @brief A capture owns copied overrides and visual metadata after the live registry is released.
TEST(canvas_resources, retained_snapshot_owns_sparse_metadata)
{
    auto memory = std::make_shared<failing_resource>();
    std::weak_ptr<std::pmr::memory_resource> lifetime = memory;
    element_snapshot retained;
    {
        element_tree tree{{}, memory};
        const auto root = tree.create();
        tree.set_panel(root, canvas_layout{});
        const auto child = tree.create(root);
        canvas_slot slot;
        slot.horizontal.offset_min = 7;
        tree.set_canvas_slot(child, slot);
        tree.set_visual(child, {{0, 1}});
        retained = tree.snapshot();
        tree.reset_canvas_slot(child);
        tree.set_visual(child, {});
        tree.remove(root);
        memory.reset();
    }

    ASSERT_FALSE(lifetime.expired());
    ASSERT_EQ(retained.canvas_slots().size(), 1U);
    EXPECT_EQ(retained.canvas_slots()[0].slot.horizontal.offset_min, 7.f);
    EXPECT_EQ(retained.elements()[1].visual, (visual_style{{0, 1}}));
    retained = {};
    EXPECT_TRUE(lifetime.expired());
}

/// @brief Every allocation boundary in changed-root preparation preserves the previous cache.
TEST(canvas_resources, allocation_failures_preserve_previous_geometry_and_allow_retry)
{
    jobs::job_system pool{1};
    element_tree tree;
    const auto root = tree.create();
    tree.set_panel(root, canvas_layout{});
    const auto child = tree.create(root);
    tree.set_viewport({0, 0, 100, 50});
    canvas_slot slot;
    slot.horizontal.offset_min = 10;
    tree.set_canvas_slot(child, slot);
    const auto original = tree.snapshot();
    slot.horizontal.offset_min = 20;
    tree.set_canvas_slot(child, slot);
    const auto changed = tree.snapshot();
    auto counter = std::make_shared<failing_resource>();
    std::size_t allocations = 0;
    {
        layout_engine engine{pool.dispatcher(), {}, counter};
        engine.calculate(original);
        const auto before = counter->calls;
        engine.calculate(changed);
        allocations = counter->calls - before;
    }

    for (std::size_t failure = 0; failure < allocations; ++failure)
    {
        SCOPED_TRACE(failure);
        auto memory = std::make_shared<failing_resource>();
        layout_engine engine{pool.dispatcher(), {}, memory};
        engine.calculate(original);
        const auto baseline = memory->live_bytes;
        memory->remaining = failure;
        EXPECT_THROW(engine.calculate(changed), std::bad_alloc);
        EXPECT_EQ(memory->live_bytes, baseline);
        EXPECT_EQ(require_geometry(engine.calculate(original).find(child)).bounds.x, 10.f);
        memory->remaining = std::numeric_limits<std::size_t>::max();
        EXPECT_EQ(require_geometry(engine.calculate(changed).find(child)).bounds.x, 20.f);
    }
}

/// @brief Typed policies and sparse slot inputs use the same algorithms on both execution paths.
TEST(canvas_resources, parallel_and_sequential_geometry_match)
{
    jobs::job_system pool{4};
    element_tree tree;
    for (int index = 0; index < 8; ++index)
    {
        const auto root = tree.create();
        tree.set_panel(root, canvas_layout{});
        const auto child = tree.create(root);
        canvas_slot slot;
        slot.horizontal = {0.25f, 0.75f, static_cast<float>(index),
                           2,     0.5f,  canvas_sizing::stretch};
        tree.set_canvas_slot(child, slot);
    }

    tree.set_viewport({3, 4, 200, 100});
    const auto input = tree.snapshot();
    layout_engine serial{pool.dispatcher(), {65536, 4096, 1}};
    layout_engine parallel{pool.dispatcher(), {65536, 1, 4}};
    const auto expected = serial.calculate(input);
    const auto result = parallel.calculate(input);
    ASSERT_EQ(expected.items().size(), result.items().size());
    EXPECT_TRUE(
        std::equal(expected.items().begin(), expected.items().end(), result.items().begin()));
    EXPECT_GT(parallel.statistics().admitted_jobs, 0U);
}
} // namespace atlas::ui::tests
