/**
 * @file
 * @brief Behavioral coverage for mixed priority with isolated scenario ownership and cleanup.
 */

#include "job_system_fixture.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <vector>

namespace atlas::jobs::tests
{
/// @brief Scenario state for mixed priority tests, isolated per test and released through RAII.
class mixed_priority_tests : public testing::TestWithParam<std::size_t>
{
  protected:
    /// @brief Construct a clean per-test runtime before the scenario starts.
    void SetUp() override
    {
        job_system_options options;
        options.worker_count = GetParam();
        options.trace_capacity = 192;
        system = std::make_unique<job_system>(options);
        std::vector<std::future<void>> started;
        for (std::size_t index = 0; index < GetParam(); ++index)
        {
            auto signal = std::make_shared<std::promise<void>>();
            started.push_back(signal->get_future());
            system->submit(
                [this, signal]
                {
                    signal->set_value();
                    gate.wait();
                });
        }

        for (auto& signal : started)
        {
            ASSERT_EQ(signal.wait_for(std::chrono::seconds{3}), std::future_status::ready);
        }
    }

    /// @brief Drain and join per-test work before releasing fixture-owned data.
    void TearDown() override
    {
        gate.open();
        system->shutdown();
    }

    /// @brief Scenario-owned system used to observe behavior and cleanup without sharing mutable
    /// state between tests.
    std::unique_ptr<job_system> system;
    /// @brief Scenario-owned gate used to observe behavior and cleanup without sharing mutable
    /// state between tests.
    signal_gate gate;
};

/// @brief Verify ready classes progress with bounded dispatch gaps and no lost work.
TEST_P(mixed_priority_tests, ready_classes_progress_with_bounded_dispatch_gaps_and_no_lost_work)
{
    std::atomic<std::size_t> executed{0};
    for (const auto priority : {job_priority::high, job_priority::normal, job_priority::background})
    {
        auto jobs = system->dispatcher().with_options({.priority = priority});
        for (int index = 0; index < 64; ++index)
        {
            jobs.run("mixed", &executed,
                     [](job_context&, std::atomic<std::size_t>*& total)
                     {
                         total->fetch_add(1);
                     });
        }
    }

    const auto queued = system->statistics();
    EXPECT_EQ(queued.queued_by_priority, (std::array<std::size_t, 3>{64, 64, 64}));
    gate.open();
    system->wait_idle();
    EXPECT_EQ(executed.load(), 192U);
    auto report = system->trace_report();
    ASSERT_EQ(report.tasks.size(), 192U);
    EXPECT_EQ(report.pending, 0U);
    EXPECT_EQ(report.dropped, 0U);
    std::ranges::sort(report.tasks, {}, &task_trace::dispatch_index);
    std::array<std::uint64_t, 3> last{GetParam(), GetParam(), GetParam()};
    std::array<std::size_t, 3> counts{};
    std::array<std::size_t, 3> full_round{};
    std::size_t position = 0;
    for (const auto& task : report.tasks)
    {
        const auto group = static_cast<std::size_t>(task.priority);
        // Initial credits may already be spent by the coordinator's held jobs.
        EXPECT_LE(task.dispatch_index - last[group], counts[group] ? 7U : 13U);
        last[group] = task.dispatch_index;
        ++counts[group];
        if (position >= 14 && position < 21)
        {
            ++full_round[group];
        }

        ++position;
        EXPECT_EQ(task.status, job_status::succeeded);
    }

    EXPECT_EQ(counts, (std::array<std::size_t, 3>{64, 64, 64}));
    EXPECT_EQ(full_round, (std::array<std::size_t, 3>{4, 2, 1}));
    const auto complete = system->statistics();
    EXPECT_EQ(complete.submitted, complete.completed);
    EXPECT_EQ(complete.dispatched_by_priority,
              (std::array<std::size_t, 3>{64, 64 + GetParam(), 64}));
}

INSTANTIATE_TEST_SUITE_P(worker_counts, mixed_priority_tests, testing::Values(1U, 2U, 4U));
} // namespace atlas::jobs::tests
