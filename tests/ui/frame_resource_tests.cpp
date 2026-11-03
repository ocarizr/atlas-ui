/** @file @brief Frame/snapshot allocation failures and retained publication lifetimes. */
#include "resource_test_support.hpp"
#include <array>
#include <atlas/jobs/job_system.hpp>
#include <atlas/ui/element_tree.hpp>
#include <atlas/ui/frame_executor.hpp>
#include <gtest/gtest.h>

namespace atlas::ui::tests
{
/// @brief A failed output allocation admits no jobs and retains no partially constructed result.
TEST(frame_resources, failed_output_allocation_latches_without_admission_or_leaks)
{
    jobs::job_system pool{2};
    auto memory = std::make_shared<failing_resource>();
    frame_executor executor{pool.dispatcher(), {}, memory};
    executor.begin_frame(1);
    const auto retained = memory->live_bytes;
    memory->remaining = 0;
    std::array input{1};
    EXPECT_THROW(executor.run_phase<int>("allocation", std::span<const int>{input},
                                         [](const phase_range<int, int>&)
                                         {
                                             ADD_FAILURE();
                                         }),
                 std::bad_alloc);
    EXPECT_EQ(memory->live_bytes, retained);
    EXPECT_EQ(executor.statistics().admitted_jobs, 0U);
    EXPECT_TRUE(executor.statistics().failed);
}

/// @brief Result ownership survives the executor, and moved-from queries are harmless.
TEST(frame_resources, published_output_retains_its_pmr_resource)
{
    jobs::job_system pool{1};
    auto memory = std::make_shared<failing_resource>();
    std::weak_ptr<std::pmr::memory_resource> lifetime = memory;
    phase_results<int> retained;
    {
        frame_executor executor{pool.dispatcher(), {}, memory};
        memory.reset();
        executor.begin_frame(1);
        const std::array input{5};
        auto result = executor.run_phase<int>("copy", std::span<const int>{input},
                                              [](const phase_range<int, int>& range)
                                              {
                                                  range.outputs[0] = range.inputs[0];
                                              });
        retained = std::move(result);
        // Deliberately verify the documented safe moved-from query contract.
        // NOLINTBEGIN(clang-analyzer-cplusplus.Move,bugprone-use-after-move)
        EXPECT_TRUE(result.values().empty());
        EXPECT_EQ(result.identity().frame, 0U);
        // NOLINTEND(clang-analyzer-cplusplus.Move,bugprone-use-after-move)
        executor.end_frame();
    }
    ASSERT_FALSE(lifetime.expired());
    EXPECT_EQ(retained.values()[0], 5);
    retained = {};
    EXPECT_TRUE(lifetime.expired());
}

/// @brief Failed snapshot preparation neither acknowledges dirtiness nor changes topology.
TEST(tree_resources, snapshot_failure_preserves_live_state)
{
    auto memory = std::make_shared<failing_resource>();
    element_tree tree{{4}, memory};
    const auto target = tree.create();
    const auto retained = memory->live_bytes;
    memory->remaining = 0;
    EXPECT_THROW(tree.snapshot(), std::bad_alloc);
    EXPECT_EQ(memory->live_bytes, retained);
    EXPECT_EQ(tree.size(), 1U);
    EXPECT_TRUE(tree.dirty(target).layout);
}
} // namespace atlas::ui::tests
