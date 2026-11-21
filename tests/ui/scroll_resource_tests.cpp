/** @file @brief Scroll publication failures preserve the previous immutable packet and cache. */
#include "resource_test_support.hpp"
#include "scroll_test_support.hpp"
#include <atlas/jobs/execution.hpp>

namespace atlas::ui::tests
{
/// @brief Inject every offset-update allocation failure, including sparse metadata allocation.
TEST_F(scroll_fixture, AllocationFailurePreservesCacheAndAllowsRetry)
{
    auto probe = std::make_shared<failing_resource>();
    std::size_t allocations = 0;
    {
        layout_engine engine{workers.dispatcher(), {}, probe};
        (void)engine.calculate(tree.snapshot());
        offset({0, 40});
        const auto before = probe->calls;
        (void)engine.calculate(tree.snapshot());
        allocations = probe->calls - before;
    }

    EXPECT_GT(allocations, 0U);
    for (std::size_t failure = 0; failure < allocations; ++failure)
    {
        SCOPED_TRACE(failure);
        auto memory = std::make_shared<failing_resource>();
        layout_engine engine{workers.dispatcher(), {}, memory};
        offset({});
        const auto baseline = tree.snapshot();
        const auto retained = engine.calculate(baseline);
        const auto live = memory->live_bytes;
        offset({0, 40});
        memory->remaining = failure;
        EXPECT_THROW(engine.calculate(tree.snapshot()), std::bad_alloc);
        EXPECT_EQ(memory->live_bytes, live);
        EXPECT_EQ(scrolling(retained).offset.y, 0);
        memory->remaining = std::numeric_limits<std::size_t>::max();
        EXPECT_EQ(engine.calculate(baseline).revision(), retained.revision());
        EXPECT_EQ(scrolling(engine.calculate(tree.snapshot())).offset.y, 40);
        EXPECT_EQ(engine.statistics().measured_nodes, 0U);
    }
}

/// @brief A cancelled update cannot publish new scroll metadata or invalidate a retained result.
TEST_F(scroll_fixture, CancellationAndStalePublicationDoNotPublishPartialState)
{
    const auto retained = layout.update(tree, {0, 0, 100, 80});
    offset({0, 40});
    std::stop_source stop;
    stop.request_stop();
    jobs::job_options options;
    options.cancellation = stop.get_token();
    EXPECT_THROW(layout.calculate(tree.snapshot(), options), jobs::job_cancelled);
    EXPECT_THROW(tree.publish_layout(retained), std::invalid_argument);
    EXPECT_FALSE(tree.computed(viewport));
    EXPECT_EQ(scrolling(retained).offset.y, 0);
    EXPECT_EQ(scrolling(layout.update(tree, {0, 0, 100, 80})).offset.y, 40);
}

/// @brief Independent scroll roots use the existing jobs path with exact numeric equality.
TEST_F(scroll_fixture, SequentialAndParallelForestsPublishIdenticalScrollMetadata)
{
    for (unsigned index = 0; index < 12; ++index)
    {
        const auto root = tree.create();
        tree.set_panel(root, scroll_layout{true, true, {31, 47}});
        const auto child = rectangle(root);
        auto style = tree.layout(child);
        style.intrinsic = {300, 500};
        tree.set_layout(child, style);
    }

    const auto source = tree.snapshot();
    layout_engine parallel{workers.dispatcher(), {.min_parallel_nodes = 1, .max_chunks = 4}};
    const auto reference = layout.calculate(source);
    const auto result = parallel.calculate(source);
    EXPECT_GT(parallel.statistics().admitted_jobs, 0U);
    EXPECT_EQ(std::vector(reference.items().begin(), reference.items().end()),
              std::vector(result.items().begin(), result.items().end()));
    EXPECT_EQ(std::vector(reference.scrolls().begin(), reference.scrolls().end()),
              std::vector(result.scrolls().begin(), result.scrolls().end()));
}
} // namespace atlas::ui::tests
