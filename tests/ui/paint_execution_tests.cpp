/** @file @brief Joined paint ranges match serial geometry under bounded scheduling and failure. */
#include "paint_test_support.hpp"
#include "resource_test_support.hpp"
#include <algorithm>
#include <random>

namespace atlas::ui::tests
{
class paint_execution_test : public ::testing::TestWithParam<paint_execution>
{
}; ///< Run identical contracts for independent roots and arbitrary painter ranges.

/// @brief Verify seeded nested contexts match serial across workers and chunk caps.
TEST_P(paint_execution_test, SeededNestedContextsMatchSerialAcrossWorkersAndChunkCaps)
{
    std::mt19937 random{11470};
    element_tree tree;
    for (unsigned r = 0; r < 8; ++r)
    {
        const auto root = tree.create();
        for (unsigned p = 0; p < 12; ++p)
        {
            const auto panel = tree.create(root);
            paint_style parent;
            parent.layer = static_cast<std::int32_t>(random() % 5);
            parent.clip = p % 2 == 0;
            parent.translation = {static_cast<float>(p), static_cast<float>(p)};
            tree.set_paint(panel, parent);
            for (unsigned i = 0; i < 8; ++i)
            {
                const auto leaf = tree.create(panel);
                paint_style style;
                style.fill = true;
                style.layer = static_cast<std::int32_t>(random() % 9);
                style.color = {0.1f, 0.2f, 0.3f, 0.5f};
                style.texture = i % 3;
                tree.set_paint(leaf, style);
            }
        }
    }

    tree.set_viewport({0, 0, 500, 400});
    const auto input = tree.snapshot();
    for (const unsigned threads : {1u, 2u, 4u})
    {
        jobs::job_system workers{threads};
        layout_engine layout{workers.dispatcher()};
        const auto geometry = layout.calculate(input);
        paint_engine serial{workers.dispatcher()};
        const auto reference = serial.calculate(input, geometry);
        for (const unsigned cap : {2u, 8u, 16u})
        {
            paint_engine parallel{workers.dispatcher(), {65536, 1, cap, GetParam()}};
            const auto output = parallel.calculate(input, geometry);
            EXPECT_TRUE(std::ranges::equal(reference.quads(), output.quads()));
            EXPECT_GT(parallel.statistics().admitted_jobs, 1);
            EXPECT_LE(parallel.statistics().admitted_jobs, cap + 1);
        }
    }
}

INSTANTIATE_TEST_SUITE_P(Policies, paint_execution_test,
                         ::testing::Values(paint_execution::roots, paint_execution::chunks));

/// @brief Verify mismatched stale and foreign geometry are rejected.
TEST_F(paint_fixture, MismatchedStaleAndForeignGeometryAreRejected)
{
    const auto node = rectangle();
    (void)capture();
    const auto source = tree.snapshot();
    const auto geometry = layout.calculate(source);
    auto style = tree.layout(node);
    style.intrinsic = {2, 3};
    tree.set_layout(node, style);
    EXPECT_THROW(paint.calculate(tree.snapshot(), geometry), std::invalid_argument);
    element_tree foreign;
    EXPECT_THROW(paint.calculate(foreign.snapshot(), geometry), std::invalid_argument);
    EXPECT_TRUE(paint.statistics().failed);
}

/// @brief Verify pre cancelled calculation publishes nothing and can be retried.
TEST_F(paint_fixture, PreCancelledCalculationPublishesNothingAndCanBeRetried)
{
    (void)rectangle();
    (void)capture();
    const auto source = tree.snapshot();
    const auto geometry = layout.calculate(source);
    std::stop_source stop;
    stop.request_stop();
    jobs::job_options options;
    options.cancellation = stop.get_token();
    EXPECT_THROW(paint.calculate(source, geometry, options), jobs::job_cancelled);
    EXPECT_TRUE(paint.statistics().failed);
    EXPECT_EQ(paint.calculate(source, geometry).quads().size(), 1);
}

/// @brief Verify every observed pmr allocation failure releases scratch and keeps prior packet.
TEST_F(paint_fixture, EveryObservedPmrAllocationFailureReleasesScratchAndKeepsPriorPacket)
{
    (void)rectangle();
    (void)capture();
    const auto source = tree.snapshot();
    const auto geometry = layout.calculate(source);
    const auto memory = std::make_shared<failing_resource>();
    {
        paint_engine checked{workers.dispatcher(), {}, memory};
        const auto reference = checked.calculate(source, geometry);
        const auto live = memory->live_bytes;
        const auto before = memory->calls;
        {
            (void)checked.calculate(source, geometry);
        }
        const auto allocations = memory->calls - before;
        for (std::size_t i = 0; i < allocations; ++i)
        {
            memory->remaining = i;
            EXPECT_THROW(checked.calculate(source, geometry), std::bad_alloc);
            memory->remaining = std::numeric_limits<std::size_t>::max();
            EXPECT_EQ(memory->live_bytes, live);
            ASSERT_EQ(reference.quads().size(), 1);
        }
    }
    EXPECT_EQ(memory->live_bytes, 0);
}
} // namespace atlas::ui::tests
