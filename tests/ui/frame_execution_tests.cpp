/** @file @brief Joined phase results match sequential execution and serial dispatch ordering. */
#include <algorithm>
#include <array>
#include <atlas/jobs/job_system.hpp>
#include <atlas/ui/frame_executor.hpp>
#include <gtest/gtest.h>
#include <numeric>
#include <thread>
#include <vector>

namespace atlas::ui::tests
{
/// @brief A deterministic capture-free kernel using only its immutable slice and exclusive output.
void transform(const phase_range<int, int>& range)
{
    for (std::size_t index = 0; index < range.inputs.size(); ++index)
    {
        range.outputs[index] = range.inputs[index] * 7 + static_cast<int>(range.first + index);
    }
}

/// @brief Both paths produce identical ordered output, with a hard bound on attached jobs.
TEST(frame_execution, sequential_and_parallel_results_are_equivalent)
{
    jobs::job_system pool{4};
    frame_limits policy;
    policy.min_parallel_items = 1;
    policy.grain = 3;
    policy.max_chunks = 4;
    frame_executor parallel{pool.dispatcher(), policy};
    policy.min_parallel_items = 1000;
    frame_executor sequential{pool.dispatcher(), policy};
    std::vector<int> inputs(37);
    std::iota(inputs.begin(), inputs.end(), 1);
    parallel.begin_frame(1);
    sequential.begin_frame(1);
    const auto result = parallel.run_phase<int>("prepare", std::span<const int>{inputs}, transform);
    const auto reference =
        sequential.run_phase<int>("prepare", std::span<const int>{inputs}, transform);
    EXPECT_TRUE(
        std::equal(result.values().begin(), result.values().end(), reference.values().begin()));
    EXPECT_EQ(result.identity().frame, 1U);
    EXPECT_EQ(result.identity().ordinal, 1U);
    EXPECT_EQ(parallel.statistics().admitted_jobs, 5U);
    EXPECT_EQ(sequential.statistics().admitted_jobs, 0U);
    parallel.end_frame();
    sequential.end_frame();
}

/// @brief Serial producers and consumers run on the owner, with a join between them.
TEST(frame_execution, serial_dispatch_surrounds_joined_work)
{
    jobs::job_system pool{2};
    frame_limits policy;
    policy.min_parallel_items = 1;
    policy.grain = 1;
    frame_executor executor{pool.dispatcher(), policy};
    const auto owner = std::this_thread::get_id();
    std::array inputs{1, 2, 3};
    executor.begin_frame(9);
    executor.dispatch("tick",
                      [&]
                      {
                          EXPECT_EQ(std::this_thread::get_id(), owner);
                          inputs[0] = 4;
                          EXPECT_THROW(executor.end_frame(), std::logic_error);
                      });
    const auto result = executor.run_phase<int>("prepare", std::span<const int>{inputs}, transform);
    executor.dispatch("publish",
                      [&]
                      {
                          EXPECT_EQ(std::this_thread::get_id(), owner);
                          EXPECT_EQ(result.values()[0], 28);
                          EXPECT_EQ(result.values()[2], 23);
                      });
    executor.end_frame();
    EXPECT_TRUE(executor.statistics().closed);
    EXPECT_EQ(executor.statistics().phases, 3U);
    EXPECT_THROW(executor.dispatch("late",
                                   []
                                   {
                                   }),
                 std::logic_error);
}

/// @brief Empty work has a phase identity but does not submit jobs or invoke a kernel.
TEST(frame_execution, empty_work_does_not_admit_jobs)
{
    jobs::job_system pool{1};
    frame_executor executor{pool.dispatcher()};
    executor.begin_frame(1);
    const auto result = executor.run_phase<int>("empty", std::span<const int>{},
                                                [](const phase_range<int, int>&)
                                                {
                                                    ADD_FAILURE() << "empty kernel invoked";
                                                });
    EXPECT_TRUE(result.values().empty());
    EXPECT_EQ(executor.statistics().admitted_jobs, 0U);
    executor.end_frame();
}
} // namespace atlas::ui::tests
