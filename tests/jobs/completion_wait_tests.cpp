/**
 * @file
 * @brief Behavioral coverage for completion wait with isolated scenario ownership and cleanup.
 */

#include "job_system_fixture.hpp"

#include <barrier>
#include <thread>
#include <vector>

namespace atlas::jobs::tests
{
namespace
{
/// @brief Scenario state for completion result, isolated per test and released through RAII.
struct completion_result
{
    /// @brief Scenario-owned value used to observe behavior and cleanup without sharing mutable
    /// state between tests.
    int value{0};
    /// @brief Scenario-owned lifetime used to observe behavior and cleanup without sharing mutable
    /// state between tests.
    std::shared_ptr<int> lifetime{std::make_shared<int>(42)};
};

/// @brief Dispatch a controlled source outcome for callback-filter scenarios.
task_handle submit_result(const job_dispatcher& jobs, job_status outcome,
                          const std::shared_ptr<completion_result>& result, signal_gate& gate)
{
    /// @brief Scenario state for input, isolated per test and released through RAII.
    struct input
    {
        /// @brief Scenario-owned outcome used to observe behavior and cleanup without sharing
        /// mutable state between tests.
        job_status outcome;
        /// @brief Scenario-owned result used to observe behavior and cleanup without sharing
        /// mutable state between tests.
        std::shared_ptr<completion_result> result;
        /// @brief Scenario-owned gate used to observe behavior and cleanup without sharing mutable
        /// state between tests.
        signal_gate* gate;
        /// @brief Scenario-owned lifetime used to observe behavior and cleanup without sharing
        /// mutable state between tests.
        std::shared_ptr<int> lifetime;
    };

    auto source =
        jobs.run("publish", input{outcome, result, &gate, std::move(result->lifetime)},
                 [](job_context&, input& data)
                 {
                     data.gate->wait();
                     data.result->value = 42;
                     if (data.outcome == job_status::failed || data.outcome == job_status::skipped)
                     {
                         throw std::runtime_error{"published failure"};
                     }

                     if (data.outcome == job_status::cancelled)
                     {
                         throw job_cancelled{};
                     }
                 });
    if (outcome == job_status::skipped)
    {
        return jobs.after(source, "skip",
                          [](job_context&)
                          {
                              FAIL();
                          });
    }

    return source;
}

/// @brief Observe the notification result and verify the scenario's expected delivery.
void expect_completion(const task_handle& handle, job_status outcome,
                       const completion_result& result, const std::weak_ptr<int>& lifetime)
{
    for (int repeat = 0; repeat < 3; ++repeat)
    {
        if (outcome == job_status::succeeded)
        {
            EXPECT_NO_THROW(handle.wait());
        }
        else
        {
            EXPECT_THROW(handle.wait(), job_error);
        }

        EXPECT_EQ(handle.status(), outcome);
        EXPECT_EQ(result.value, 42);
        EXPECT_TRUE(lifetime.expired());
        EXPECT_GE(handle.timing().total.count(), 0);
        if (outcome == job_status::cancelled)
        {
            EXPECT_THROW(std::rethrow_exception(handle.exception()), job_cancelled);
        }
        else if (outcome != job_status::succeeded)
        {
            EXPECT_THROW(std::rethrow_exception(handle.exception()), std::runtime_error);
        }
    }
}
} // namespace

/// @brief Scenario state for completion wait tests, isolated per test and released through RAII.
class completion_wait_tests : public testing::TestWithParam<job_status>
{
};

/// @brief Verify concurrent waiters observe payload cleanup and the same result.
TEST_P(completion_wait_tests, concurrent_waiters_observe_payload_cleanup_and_the_same_result)
{
    job_system system{2};
    signal_gate gate;
    auto result = std::make_shared<completion_result>();
    const std::weak_ptr<int> lifetime = result->lifetime;
    auto task = submit_result(system.dispatcher(), GetParam(), result, gate);
    std::barrier ready{5};
    std::vector<std::jthread> waiters;
    waiters.reserve(4);
    for (int index = 0; index < 4; ++index)
    {
        waiters.emplace_back(
            [&, outcome = GetParam()]
            {
                ready.arrive_and_wait();
                expect_completion(task, outcome, *result, lifetime);
            });
    }

    ready.arrive_and_wait();
    gate.open();
    waiters.clear();
}

/// @brief Verify concurrent waiters can observe results after runtime destruction.
TEST_P(completion_wait_tests, concurrent_waiters_can_observe_results_after_runtime_destruction)
{
    task_handle task;
    auto result = std::make_shared<completion_result>();
    const std::weak_ptr<int> lifetime = result->lifetime;
    {
        job_system system{2};
        signal_gate gate;
        task = submit_result(system.dispatcher(), GetParam(), result, gate);
        gate.open();
        system.shutdown();
    }
    std::vector<std::jthread> waiters;
    waiters.reserve(4);
    for (int index = 0; index < 4; ++index)
    {
        waiters.emplace_back(
            [&, outcome = GetParam()]
            {
                expect_completion(task, outcome, *result, lifetime);
            });
    }

    waiters.clear();
}

/// @brief Verify waiters can overlap runtime shutdown and destruction.
TEST_P(completion_wait_tests, waiters_can_overlap_runtime_shutdown_and_destruction)
{
    signal_gate gate;
    auto result = std::make_shared<completion_result>();
    const std::weak_ptr<int> lifetime = result->lifetime;
    std::barrier ready{6};
    std::promise<task_handle> published;
    auto source = published.get_future();
    std::jthread owner{
        [&, outcome = GetParam()]
        {
            job_system system{2};
            published.set_value(submit_result(system.dispatcher(), outcome, result, gate));
            ready.arrive_and_wait();
        }};
    const auto task = source.get();
    std::vector<std::jthread> waiters;
    waiters.reserve(4);
    for (int index = 0; index < 4; ++index)
    {
        waiters.emplace_back(
            [&, outcome = GetParam()]
            {
                ready.arrive_and_wait();
                expect_completion(task, outcome, *result, lifetime);
            });
    }

    ready.arrive_and_wait();
    gate.open();
    waiters.clear();
    owner.join();
}

INSTANTIATE_TEST_SUITE_P(outcomes, completion_wait_tests,
                         testing::Values(job_status::succeeded, job_status::failed,
                                         job_status::cancelled, job_status::skipped),
                         [](const testing::TestParamInfo<job_status>& info)
                         {
                             switch (info.param)
                             {
                                 case job_status::succeeded:
                                     return "Success";
                                 case job_status::failed:
                                     return "Failure";
                                 case job_status::cancelled:
                                     return "Cancelled";
                                 default:
                                     return "Skipped";
                             }
                         });
} // namespace atlas::jobs::tests
