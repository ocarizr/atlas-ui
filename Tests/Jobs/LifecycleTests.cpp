#include "JobSystemFixture.hpp"

#include <atomic>
#include <stdexcept>
#include <thread>

namespace atlas::jobs::tests
{
TEST_F(JobSystemFixture, ShutdownDrainsAcceptedJobsAndRejectsNewWork)
{
    std::atomic<int> executed{0};
    for (int index = 0; index < 64; ++index)
    {
        system.submit([&] { ++executed; });
    }
    system.shutdown();
    EXPECT_EQ(executed.load(), 64);
    EXPECT_THROW(system.submit([] {}), std::logic_error);
    EXPECT_NO_THROW(system.shutdown());
    const auto statistics = system.statistics();
    EXPECT_EQ(statistics.submitted, statistics.completed);
    EXPECT_EQ(statistics.queued, 0U);
    EXPECT_EQ(statistics.running, 0U);
}

TEST(JobSystemLifecycleTests, DestructionDrainsOutstandingWork)
{
    std::atomic<int> executed{0};
    {
        JobSystem system{2};
        for (int index = 0; index < 32; ++index)
        {
            system.submit([&] { ++executed; });
        }
    }
    EXPECT_EQ(executed.load(), 32);
}

TEST_F(JobSystemFixture, CompletionHandlesCanBeObservedMoreThanOnce)
{
    const auto job = system.submit([] {});
    EXPECT_NO_THROW(system.wait(job));
    EXPECT_NO_THROW(system.wait(job));
}

TEST_F(JobSystemFixture, ConcurrentCoordinatorShutdownCallsAreSerialized)
{
    std::atomic<int> executed{0};
    for (int index = 0; index < 64; ++index)
    {
        system.submit([&] { ++executed; });
    }
    {
        std::jthread first{[&] { system.shutdown(); }};
        std::jthread second{[&] { system.shutdown(); }};
    }
    EXPECT_EQ(executed.load(), 64);
}
} // namespace atlas::jobs::tests
