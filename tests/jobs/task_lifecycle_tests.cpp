/**
 * @file
 * @brief Behavioral coverage for task lifecycle with isolated scenario ownership and cleanup.
 */

#include "job_system_fixture.hpp"

#include <atomic>
#include <thread>
#include <vector>

namespace atlas::jobs::tests
{
/// @brief Verify default handles reject observation and waits.
TEST(task_handle_tests, default_handles_reject_observation_and_waits)
{
    task_handle invalid;
    EXPECT_THROW(invalid.wait(), std::invalid_argument);
    EXPECT_THROW((void)invalid.ready(), std::invalid_argument);
    EXPECT_THROW((void)invalid.status(), std::invalid_argument);
    EXPECT_THROW((void)invalid.name(), std::invalid_argument);
    EXPECT_THROW((void)invalid.exception(), std::invalid_argument);
    EXPECT_THROW((void)invalid.timing(), std::invalid_argument);
}

/// @brief Verify completed handles outlive the pool but dispatcher views do not keep it alive.
TEST(task_handle_tests,
     completed_handles_outlive_the_pool_but_dispatcher_views_do_not_keep_it_alive)
{
    task_handle completed;
    auto expired = [&]
    {
        job_system system{1};
        auto jobs = system.dispatcher();
        completed = jobs.run("last",
                             [](job_context&)
                             {
                             });
        return jobs;
    }();
    EXPECT_NO_THROW(completed.wait());
    EXPECT_EQ(completed.status(), job_status::succeeded);
    EXPECT_THROW(expired.run("late",
                             [](job_context&)
                             {
                             }),
                 std::logic_error);
}

/// @brief Verify shutdown drains child creation from already accepted roots.
TEST_F(job_system_fixture, shutdown_drains_child_creation_from_already_accepted_roots)
{
    /// @brief Scenario state for input, isolated per test and released through RAII.
    struct input
    {
        /// @brief Scenario-owned gate used to observe behavior and cleanup without sharing mutable
        /// state between tests.
        signal_gate* gate;
        /// @brief Scenario-owned executed used to observe behavior and cleanup without sharing
        /// mutable state between tests.
        std::shared_ptr<std::atomic<int>> executed;
    };

    auto jobs = system.dispatcher();
    auto executed = std::make_shared<std::atomic<int>>(0);
    auto root = jobs.run("root", input{&gate, executed},
                         [](job_context& context, input& data)
                         {
                             data.gate->wait();
                             context.spawn("accepted child", data.executed,
                                           [](job_context&, auto& result)
                                           {
                                               ++*result;
                                           });
                         });
    std::jthread stopping{[this]
                          {
                              system.shutdown();
                          }};

    /// @brief Scenario state for release gate, isolated per test and released through RAII.
    struct release_gate
    {
        /// @brief Scenario-owned gate used to observe behavior and cleanup without sharing mutable
        /// state between tests.
        signal_gate& gate;

        /// @brief Release release gate resources and complete its scoped cleanup.
        ~release_gate()
        {
            try
            {
                gate.open();
            }
            catch (...)
            {
                std::terminate();
            }
        }
    } release{gate};

    bool rejected = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
    while (!rejected && std::chrono::steady_clock::now() < deadline)
    {
        try
        {
            jobs.run("probe",
                     [](job_context&)
                     {
                     });
        }
        catch (const std::logic_error&)
        {
            rejected = true;
        }

        std::this_thread::yield();
    }

    EXPECT_TRUE(rejected);
    gate.open();
    stopping.join();
    root.wait();
    EXPECT_EQ(executed->load(), 1);
    EXPECT_THROW(jobs.run("after stop",
                          [](job_context&)
                          {
                          }),
                 std::logic_error);
    const auto statistics = system.statistics();
    EXPECT_EQ(statistics.submitted, statistics.completed);
    EXPECT_EQ(statistics.waiting, 0U);
}

/// @brief Verify live tasks offer status without blocking and publish timings on completion.
TEST_F(job_system_fixture,
       live_tasks_offer_status_without_blocking_and_publish_timings_on_completion)
{
    /// @brief Scenario state for input, isolated per test and released through RAII.
    struct input
    {
        /// @brief Scenario-owned gate used to observe behavior and cleanup without sharing mutable
        /// state between tests.
        signal_gate* gate;
        /// @brief Scenario-owned started used to observe behavior and cleanup without sharing
        /// mutable state between tests.
        std::shared_ptr<std::promise<void>> started;
    };

    auto started = std::make_shared<std::promise<void>>();
    auto ready = started->get_future();
    auto job = system.dispatcher().run("timed", input{&gate, started},
                                       [](job_context&, input& data)
                                       {
                                           data.started->set_value();
                                           data.gate->wait();
                                       });
    EXPECT_EQ(ready.wait_for(std::chrono::seconds{3}), std::future_status::ready);
    EXPECT_FALSE(job.ready());
    EXPECT_FALSE(job.exception());
    EXPECT_THROW((void)job.timing(), std::logic_error);
    gate.open();
    job.wait();
    const auto timing = job.timing();
    EXPECT_GE(timing.dependency_wait.count(), 0);
    EXPECT_GE(timing.queue_wait.count(), 0);
    EXPECT_GE(timing.execution.count(), 0);
    EXPECT_GE(timing.join.count(), 0);
    EXPECT_GE(timing.total, timing.execution + timing.join);
}

/// @brief Verify dispatcher copies share one pool across concurrent producers.
TEST_F(job_system_fixture, dispatcher_copies_share_one_pool_across_concurrent_producers)
{
    const auto jobs = system.dispatcher();
    auto executed = std::make_shared<std::atomic<int>>(0);
    {
        std::vector<std::jthread> producers;
        producers.reserve(4);
        for (int producer = 0; producer < 4; ++producer)
        {
            producers.emplace_back(
                [jobs, executed]
                {
                    for (int index = 0; index < 64; ++index)
                    {
                        jobs.run("shared pool", executed,
                                 [](job_context&, auto& result)
                                 {
                                     ++*result;
                                 })
                            .wait();
                    }
                });
        }
    }
    EXPECT_EQ(executed->load(), 256);
    EXPECT_EQ(system.statistics().workers, 2U);
}
} // namespace atlas::jobs::tests
