/**
 * @file
 * @brief Behavioral coverage for callback outcome with isolated scenario ownership and cleanup.
 */

#include "job_system_fixture.hpp"

#include <array>

namespace atlas::jobs::tests
{
/// @brief Scenario state for callback outcome tests, isolated per test and released through RAII.
class callback_outcome_tests : public testing::TestWithParam<job_status>
{
};

namespace
{
/// @brief Return a source handle with the requested controlled terminal outcome.
task_handle completed_source(const job_dispatcher& jobs, job_status status)
{
    task_handle source;
    switch (status)
    {
        case job_status::succeeded:
            source = jobs.run("success",
                              [](job_context&)
                              {
                              });
            break;
        case job_status::cancelled:
            source = jobs.run("cancelled",
                              [](job_context&)
                              {
                                  throw job_cancelled{};
                              });
            break;
        default:
            source = jobs.run("failure",
                              [](job_context&)
                              {
                                  throw 42;
                              });
            if (status == job_status::skipped)
            {
                source = jobs.after(source, "skipped",
                                    [](job_context&)
                                    {
                                        FAIL();
                                    });
            }
            break;
    }

    if (status == job_status::succeeded)
    {
        source.wait();
    }
    else
    {
        EXPECT_THROW(source.wait(), job_error);
    }

    return source;
}
} // namespace

/// @brief Verify complete is unconditional and specific listeners match exactly.
TEST_P(callback_outcome_tests, complete_is_unconditional_and_specific_listeners_match_exactly)
{
    job_system runtime{2};
    auto jobs = runtime.dispatcher();
    auto source = completed_source(jobs, GetParam());
    std::array<int, 5> counts{};
    constexpr std::array events{completion_event::complete, completion_event::success,
                                completion_event::failure, completion_event::cancelled,
                                completion_event::skipped};

    /// @brief Scenario state for input, isolated per test and released through RAII.
    struct input
    {
        /// @brief Scenario-owned count used to observe behavior and cleanup without sharing mutable
        /// state between tests.
        int* count;
        /// @brief Scenario-owned expected used to observe behavior and cleanup without sharing
        /// mutable state between tests.
        job_status expected;
    };

    for (std::size_t index = 0; index < events.size(); ++index)
    {
        jobs.callbacks().on(events[index], source, "observe", input{&counts[index], GetParam()},
                            [](job_context&, const task_handle& result, input& data)
                            {
                                EXPECT_EQ(result.status(), data.expected);
                                ++*data.count;
                            });
    }

    runtime.wait_idle();
    EXPECT_EQ(counts[0], 1);
    const std::array outcomes{job_status::succeeded, job_status::failed, job_status::cancelled,
                              job_status::skipped};
    for (std::size_t index = 0; index < outcomes.size(); ++index)
    {
        EXPECT_EQ(counts[index + 1], GetParam() == outcomes[index] ? 1 : 0);
    }
}

INSTANTIATE_TEST_SUITE_P(terminal_states, callback_outcome_tests,
                         testing::Values(job_status::succeeded, job_status::failed,
                                         job_status::cancelled, job_status::skipped),
                         [](const testing::TestParamInfo<job_status>& info)
                         {
                             return info.param == job_status::succeeded   ? "Success"
                                    : info.param == job_status::failed    ? "Failure"
                                    : info.param == job_status::cancelled ? "Cancelled"
                                                                          : "Skipped";
                         });
} // namespace atlas::jobs::tests
