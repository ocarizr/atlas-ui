#include "job_system_fixture.hpp"

#include <atomic>
#include <stdexcept>

namespace atlas::jobs::tests
{
TEST(job_system_construction_tests, rejects_zero_workers)
{
    EXPECT_THROW(job_system{0}, std::invalid_argument);
}

TEST_F(job_system_fixture, rejects_empty_jobs_without_changing_counters)
{
    EXPECT_THROW(system.submit({}), std::invalid_argument);
    EXPECT_EQ(system.statistics().submitted, 0U);
}

TEST_F(job_system_fixture, propagates_failures_without_losing_the_worker)
{
    const auto failed = system.submit([] { throw std::runtime_error{"job failure"}; });
    EXPECT_THROW(system.wait(failed), std::runtime_error);
    std::atomic<bool> executed{false};
    system.wait(system.submit([&] { executed = true; }));
    EXPECT_TRUE(executed.load());
}

TEST_F(job_system_fixture, rejects_invalid_and_foreign_handles)
{
    EXPECT_THROW(system.wait(job_handle{}), std::invalid_argument);
    job_system other{1};
    const auto foreign = other.submit([] {});
    EXPECT_THROW(system.wait(foreign), std::invalid_argument);
}

TEST_F(job_system_fixture, rejects_worker_side_waits_rather_than_deadlocking)
{
    std::atomic<bool> rejected{false};
    const auto job = system.submit(
        [&]
        {
            try
            {
                system.wait_idle();
            }
            catch (const std::logic_error&)
            {
                rejected = true;
            }
        });
    system.wait(job);
    EXPECT_TRUE(rejected.load());
}

TEST_F(job_system_fixture, rejects_worker_side_shutdown_rather_than_joining_itself)
{
    std::atomic<bool> rejected{false};
    system.wait(system.submit(
        [&]
        {
            try
            {
                system.shutdown();
            }
            catch (const std::logic_error&)
            {
                rejected = true;
            }
        }));
    EXPECT_TRUE(rejected.load());
}

TEST_F(job_system_fixture, rejects_worker_side_handle_wait_even_when_the_job_already_completed)
{
    const auto completed = system.submit([] {});
    system.wait(completed);
    std::atomic<bool> rejected{false};
    system.wait(system.submit(
        [&]
        {
            try
            {
                system.wait(completed);
            }
            catch (const std::logic_error&)
            {
                rejected = true;
            }
        }));
    EXPECT_TRUE(rejected.load());
}
} // namespace atlas::jobs::tests
