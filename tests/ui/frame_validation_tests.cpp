/** @file @brief Frame scope, capacity, owner affinity and failure recovery contracts. */
#include <array>
#include <atlas/jobs/job_system.hpp>
#include <atlas/ui/frame_executor.hpp>
#include <gtest/gtest.h>
#include <thread>

namespace atlas::ui::tests
{
/// @brief Preflight rejection does not consume a phase identity or admit any jobs.
TEST(frame_validation, bounded_preflight_rejects_without_admission)
{
    jobs::job_system pool{2};
    frame_limits limits;
    limits.max_items = 2;
    limits.max_output_bytes = sizeof(int);
    limits.max_phases = 1;
    frame_executor executor{pool.dispatcher(), limits};
    EXPECT_THROW(executor.end_frame(), std::logic_error);
    executor.begin_frame(1);
    EXPECT_THROW(executor.begin_frame(2), std::logic_error);
    std::array input{1, 2, 3};
    const auto kernel = [](const phase_range<int, int>&)
    {
    };
    EXPECT_THROW(executor.run_phase<int>("too_many", std::span<const int>{input}, kernel),
                 std::overflow_error);
    EXPECT_THROW(executor.run_phase<int>("too_large", std::span<const int>{input}.first(2), kernel),
                 std::overflow_error);
    EXPECT_THROW(executor.dispatch("",
                                   []
                                   {
                                   }),
                 std::invalid_argument);
    EXPECT_EQ(executor.statistics().phases, 0U);
    EXPECT_THROW(executor.dispatch("null", static_cast<void (*)()>(nullptr)),
                 std::invalid_argument);
    executor.dispatch("tick",
                      []
                      {
                      });
    EXPECT_THROW(executor.dispatch("tick",
                                   []
                                   {
                                   }),
                 std::invalid_argument);
    EXPECT_THROW(executor.dispatch("extra",
                                   []
                                   {
                                   }),
                 std::overflow_error);
    EXPECT_EQ(executor.statistics().admitted_jobs, 0U);
    executor.end_frame();
    EXPECT_THROW(executor.begin_frame(1), std::invalid_argument);
}

/// @brief A failed dispatch latches the frame; only a newer frame can resume processing.
TEST(frame_validation, failed_frame_cannot_run_later_phases)
{
    jobs::job_system pool{1};
    frame_executor executor{pool.dispatcher()};
    executor.begin_frame(1);
    EXPECT_THROW(executor.dispatch("tick",
                                   []
                                   {
                                       throw std::domain_error("controlled failure");
                                   }),
                 std::domain_error);
    EXPECT_TRUE(executor.statistics().failed);
    EXPECT_THROW(executor.dispatch("publish",
                                   []
                                   {
                                       ADD_FAILURE();
                                   }),
                 std::logic_error);
    EXPECT_THROW(executor.end_frame(), std::logic_error);
    executor.begin_frame(2);
    executor.dispatch("tick",
                      []
                      {
                      });
    executor.end_frame();
    EXPECT_FALSE(executor.statistics().failed);
}

/// @brief Waiting adapters reject worker callers even when the sequential path would be selected.
TEST(frame_validation, worker_preflight_admits_no_nested_work)
{
    jobs::job_system pool{1};
    auto outer = pool.dispatcher().run("worker preflight", &pool,
                                       [](jobs::job_context&, jobs::job_system*& shared)
                                       {
                                           EXPECT_THROW((frame_executor{shared->dispatcher()}),
                                                        std::logic_error);
                                       });
    outer.wait();
}

/// @brief Foreign coordinators fail before reading mutable frame state or admitting any work.
TEST(frame_validation, foreign_thread_cannot_access_the_executor)
{
    jobs::job_system pool{1};
    frame_executor executor{pool.dispatcher()};
    std::jthread foreign{[&]
                         {
                             EXPECT_THROW(executor.begin_frame(1), std::logic_error);
                             EXPECT_THROW(executor.statistics(), std::logic_error);
                         }};
    foreign.join();
    EXPECT_EQ(executor.statistics().frame, 0U);
}

/// @brief Invalid budgets fail construction before phase storage or worker admission.
class invalid_frame_limits_test : public ::testing::TestWithParam<frame_limits>
{
};

/// @brief Named variations keep independent failure cases visible in CTest.
TEST_P(invalid_frame_limits_test, rejects_invalid_limits)
{
    jobs::job_system pool{1};
    EXPECT_THROW((frame_executor{pool.dispatcher(), GetParam()}), std::invalid_argument);
}

INSTANTIATE_TEST_SUITE_P(
    limits, invalid_frame_limits_test,
    ::testing::Values(frame_limits{0}, frame_limits{1048577}, frame_limits{4, 0},
                      frame_limits{4, 64, 0}, frame_limits{4, 64, 65}, frame_limits{4, 64, 4, 0},
                      frame_limits{4, 64, 4, 1, 0}, frame_limits{4, 64, 4, 1, 1, 0},
                      frame_limits{4, 64, 4, 1, 1, 65}),
    (
        [](const ::testing::TestParamInfo<frame_limits>& info)
        {
            constexpr std::array names{"zero_items",  "excess_items",  "zero_bytes",
                                       "zero_phases", "excess_phases", "zero_threshold",
                                       "zero_grain",  "zero_chunks",   "excess_chunks"};
            return std::string{names[info.index]};
        }));
} // namespace atlas::ui::tests
