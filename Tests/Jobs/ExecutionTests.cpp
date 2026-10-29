#include "JobSystemFixture.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <set>
#include <thread>
#include <vector>

namespace atlas::jobs::tests
{
class WorkerCountTests : public testing::TestWithParam<std::size_t>
{
};

TEST_P(WorkerCountTests, ExecutesEverySubmittedJobExactlyOnce)
{
    JobSystem system{GetParam()};
    std::array<std::atomic<int>, 128> counts{};
    std::set<std::thread::id> threads;
    std::mutex threads_mutex;
    for (std::size_t index = 0; index < counts.size(); ++index)
    {
        system.submit(
            [&, index]
            {
                ++counts[index];
                const std::lock_guard lock{threads_mutex};
                threads.insert(std::this_thread::get_id());
            });
    }
    system.wait_idle();
    for (const auto& count : counts)
    {
        EXPECT_EQ(count.load(), 1);
    }
    EXPECT_LE(threads.size(), GetParam());
    EXPECT_FALSE(threads.contains(std::this_thread::get_id()));
    EXPECT_EQ(system.statistics().submitted, counts.size());
    EXPECT_EQ(system.statistics().completed, counts.size());
}

INSTANTIATE_TEST_SUITE_P(PersistentWorkers, WorkerCountTests, testing::Values(1U, 2U, 4U));

TEST_F(JobSystemFixture, IndependentJobsRunConcurrently)
{
    auto first_started = std::make_shared<std::promise<void>>();
    auto second_started = std::make_shared<std::promise<void>>();
    auto first = first_started->get_future();
    auto second = second_started->get_future();
    system.submit(
        [this, first_started]
        {
            first_started->set_value();
            gate.wait();
        });
    system.submit(
        [this, second_started]
        {
            second_started->set_value();
            gate.wait();
        });
    EXPECT_EQ(first.wait_for(std::chrono::seconds{3}), std::future_status::ready);
    EXPECT_EQ(second.wait_for(std::chrono::seconds{3}), std::future_status::ready);
    gate.open();
    system.wait_idle();
}

TEST(WorkerReuseTests, AWorkerIsReusedAcrossSeparateSubmissions)
{
    JobSystem system{1};
    std::thread::id first;
    std::thread::id second;
    system.wait(system.submit([&] { first = std::this_thread::get_id(); }));
    system.wait(system.submit([&] { second = std::this_thread::get_id(); }));
    EXPECT_EQ(first, second);
}

TEST_F(JobSystemFixture, SupportsConcurrentProducers)
{
    std::atomic<int> executed{0};
    {
        std::vector<std::jthread> producers;
        producers.reserve(4);
        for (int producer = 0; producer < 4; ++producer)
        {
            producers.emplace_back(
                [&]
                {
                    for (int index = 0; index < 64; ++index)
                    {
                        system.submit([&] { ++executed; });
                    }
                });
        }
    }
    system.wait_idle();
    EXPECT_EQ(executed.load(), 256);
}

TEST_F(JobSystemFixture, SupportsNestedSubmissionWithoutBlockingWorkers)
{
    std::atomic<int> executed{0};
    system.submit(
        [&]
        {
            ++executed;
            system.submit([&] { ++executed; });
        });
    system.wait_idle();
    EXPECT_EQ(executed.load(), 2);
}
} // namespace atlas::jobs::tests
