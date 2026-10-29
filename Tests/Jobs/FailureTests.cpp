#include "JobSystemFixture.hpp"

#include <atomic>
#include <stdexcept>

namespace atlas::jobs::tests
{
TEST(JobSystemConstructionTests, RejectsZeroWorkers)
{
    EXPECT_THROW(JobSystem{0}, std::invalid_argument);
}

TEST_F(JobSystemFixture, RejectsEmptyJobsWithoutChangingCounters)
{
    EXPECT_THROW(system.submit({}), std::invalid_argument);
    EXPECT_EQ(system.statistics().submitted, 0U);
}

TEST_F(JobSystemFixture, PropagatesFailuresWithoutLosingTheWorker)
{
    const auto failed = system.submit([] { throw std::runtime_error{"job failure"}; });
    EXPECT_THROW(system.wait(failed), std::runtime_error);
    std::atomic<bool> executed{false};
    system.wait(system.submit([&] { executed = true; }));
    EXPECT_TRUE(executed.load());
}

TEST_F(JobSystemFixture, RejectsInvalidAndForeignHandles)
{
    EXPECT_THROW(system.wait(JobHandle{}), std::invalid_argument);
    JobSystem other{1};
    const auto foreign = other.submit([] {});
    EXPECT_THROW(system.wait(foreign), std::invalid_argument);
}

TEST_F(JobSystemFixture, RejectsWorkerSideWaitsRatherThanDeadlocking)
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

TEST_F(JobSystemFixture, RejectsWorkerSideShutdownRatherThanJoiningItself)
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

TEST_F(JobSystemFixture, RejectsWorkerSideHandleWaitEvenWhenTheJobAlreadyCompleted)
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
