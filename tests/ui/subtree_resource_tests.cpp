/** @file @brief Frontier and joined-output failures retain exact cache and allocator lifetimes. */
#include "resource_test_support.hpp"
#include "subtree_test_support.hpp"
#include <atlas/jobs/job_system.hpp>
#include <atlas/ui/layout_engine.hpp>
#include <gtest/gtest.h>

namespace atlas::ui::tests
{
/// @brief UI PMR allocations happen on the coordinator; no counter races with worker kernels.
TEST(subtree_resources, all_observed_pmr_failures_preserve_cache_and_allow_retry)
{
    jobs::job_system pool{2};
    element_tree tree;
    const auto root = append_subtree_scenario(tree, {4, 4});
    tree.set_viewport({0, 0, 100, 100});
    const auto original = tree.snapshot();
    auto style = tree.layout(tree.first_child(root));
    style.intrinsic = {20, 10};
    tree.set_layout(tree.first_child(root), style);
    const auto changed = tree.snapshot();
    auto counter = std::make_shared<failing_resource>();
    std::size_t allocations = 0;
    {
        layout_engine engine{pool.dispatcher(), {4096, 1, 4, 1}, counter};
        engine.calculate(original);
        const auto before = counter->calls;
        engine.calculate(changed);
        allocations = counter->calls - before;
    }

    ASSERT_GT(allocations, 0U);
    for (std::size_t failure = 0; failure < allocations; ++failure)
    {
        SCOPED_TRACE(failure);
        auto memory = std::make_shared<failing_resource>();
        layout_engine engine{pool.dispatcher(), {4096, 1, 4, 1}, memory};
        const auto first = engine.calculate(original);
        const auto retained = memory->live_bytes;
        memory->remaining = failure;
        EXPECT_THROW(engine.calculate(changed), std::bad_alloc);
        EXPECT_EQ(memory->live_bytes, retained);
        EXPECT_EQ(engine.calculate(original).find(root), first.find(root));
        memory->remaining = std::numeric_limits<std::size_t>::max();
        EXPECT_EQ(engine.calculate(changed).items().size(), changed.elements().size());
    }
}

/// @brief Failure inside a real worker joins attached siblings and preserves original exceptions.
TEST(subtree_resources, worker_overflow_rejects_publication_and_can_retry)
{
    jobs::job_system pool{2};
    element_tree tree;
    const auto root = append_subtree_scenario(tree, {4, 4});
    layout_engine engine{pool.dispatcher(), {4096, 1, 4, 1}};
    const auto old = engine.update(tree, {0, 0, 100, 100});
    const auto original = tree.snapshot();
    const auto column = tree.first_child(root);
    for (auto child = tree.first_child(column); child != element_handle{};
         child = tree.next_sibling(child))
    {
        auto style = tree.layout(child);
        style.intrinsic.height = std::numeric_limits<float>::max();
        tree.set_layout(child, style);
    }

    EXPECT_THROW(engine.update(tree, {0, 0, 100, 100}), std::overflow_error);
    EXPECT_TRUE(engine.statistics().failed);
    EXPECT_GT(engine.statistics().admitted_jobs, 0U);
    EXPECT_FALSE(tree.computed(root));
    EXPECT_FALSE(tree.computed(column));
    pool.wait_idle();
    EXPECT_EQ(pool.statistics().running, 0U);
    EXPECT_EQ(engine.calculate(original).find(column), old.find(column));
}

/// @brief An expired shared runtime fails before admission; pre-cancelled work admits nothing.
TEST(subtree_resources, stopped_or_expired_dispatcher_never_publishes_partial_geometry)
{
    element_tree tree;
    append_subtree_scenario(tree, {4, 4});
    tree.set_viewport({0, 0, 100, 100});
    const auto dispatcher = [&]
    {
        jobs::job_system pool{1};
        const auto client = pool.dispatcher();
        std::stop_source stop;
        stop.request_stop();
        layout_engine engine{client, {4096, 1, 4, 1}};
        EXPECT_THROW(
            engine.calculate(tree.snapshot(), {jobs::job_priority::normal, stop.get_token()}),
            jobs::job_cancelled);
        EXPECT_EQ(engine.statistics().admitted_jobs, 0U);
        return client;
    }();

    layout_engine engine{dispatcher, {4096, 1, 4, 1}};
    EXPECT_THROW(engine.calculate(tree.snapshot()), std::logic_error);
    EXPECT_EQ(engine.statistics().admitted_jobs, 0U);
}
} // namespace atlas::ui::tests
