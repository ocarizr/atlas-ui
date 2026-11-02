/**
 * @file
 * @brief Behavioral coverage for lifecycle with isolated scenario ownership and cleanup.
 */

#include "job_system_fixture.hpp"
#include <atlas/jobs/execution.hpp>

#include <atomic>
#include <stdexcept>
#include <thread>

namespace atlas::jobs::tests
{
/// @brief Adapters can validate a coordinator before accepting borrowed work that requires a join.
TEST(execution_context_tests, preflight_rejects_workers_and_accepts_coordinators)
{
    EXPECT_NO_THROW(require_coordinator_thread());
    job_system system{1};
    auto task =
        system.dispatcher().run("preflight",
                                [](job_context&)
                                {
                                    EXPECT_THROW(require_coordinator_thread(), std::logic_error);
                                });
    task.wait();
    EXPECT_NO_THROW(require_coordinator_thread());
}

/// @brief Verify shutdown drains accepted jobs and rejects new work.
TEST_F(job_system_fixture, shutdown_drains_accepted_jobs_and_rejects_new_work)
{
    std::atomic<int> executed{0};
    for (int index = 0; index < 64; ++index)
    {
        system.submit(
            [&]
            {
                ++executed;
            });
    }

    system.shutdown();
    EXPECT_EQ(executed.load(), 64);
    EXPECT_THROW(system.submit(
                     []
                     {
                     }),
                 std::logic_error);
    EXPECT_NO_THROW(system.shutdown());
    const auto statistics = system.statistics();
    EXPECT_EQ(statistics.submitted, statistics.completed);
    EXPECT_EQ(statistics.queued, 0U);
    EXPECT_EQ(statistics.running, 0U);
}

/// @brief Verify destruction drains outstanding work.
TEST(job_system_lifecycle_tests, destruction_drains_outstanding_work)
{
    std::atomic<int> executed{0};
    {
        job_system system{2};
        for (int index = 0; index < 32; ++index)
        {
            system.submit(
                [&]
                {
                    ++executed;
                });
        }
    }
    EXPECT_EQ(executed.load(), 32);
}

/// @brief Verify completion handles can be observed more than once.
TEST_F(job_system_fixture, completion_handles_can_be_observed_more_than_once)
{
    const auto job = system.submit(
        []
        {
        });
    EXPECT_NO_THROW(system.wait(job));
    EXPECT_NO_THROW(system.wait(job));
}

/// @brief Verify concurrent coordinator shutdown calls are serialized.
TEST_F(job_system_fixture, concurrent_coordinator_shutdown_calls_are_serialized)
{
    std::atomic<int> executed{0};
    for (int index = 0; index < 64; ++index)
    {
        system.submit(
            [&]
            {
                ++executed;
            });
    }

    {
        std::jthread first{[&]
                           {
                               system.shutdown();
                           }};
        std::jthread second{[&]
                            {
                                system.shutdown();
                            }};
    }
    EXPECT_EQ(executed.load(), 64);
}
} // namespace atlas::jobs::tests
