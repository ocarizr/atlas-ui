/**
 * @file
 * @brief Behavioral coverage for callback independence with isolated scenario ownership and
 * cleanup.
 */

#include "job_system_fixture.hpp"

#include <atomic>
#include <thread>

namespace atlas::jobs::tests
{
/// @brief Verify continuations can have listeners without joining their creator.
TEST(callback_independence_tests, continuations_can_have_listeners_without_joining_their_creator)
{
    job_system system{1};

    /// @brief Scenario state for report, isolated per test and released through RAII.
    struct report
    {
        /// @brief Scenario-owned notification used to observe behavior and cleanup without sharing
        /// mutable state between tests.
        task_handle notification;
        /// @brief Scenario-owned owner used to observe behavior and cleanup without sharing mutable
        /// state between tests.
        std::thread::id owner;
        /// @brief Scenario-owned parent lifetime used to observe behavior and cleanup without
        /// sharing mutable state between tests.
        std::weak_ptr<int> parent_lifetime;
        /// @brief Scenario-owned calls used to observe behavior and cleanup without sharing mutable
        /// state between tests.
        int calls{0};
    };

    auto output = std::make_shared<report>();
    auto lifetime = std::make_shared<int>(42);
    output->parent_lifetime = lifetime;

    /// @brief Scenario state for input, isolated per test and released through RAII.
    struct input
    {
        /// @brief Scenario-owned output used to observe behavior and cleanup without sharing
        /// mutable state between tests.
        std::shared_ptr<report> output;
        /// @brief Scenario-owned lifetime used to observe behavior and cleanup without sharing
        /// mutable state between tests.
        std::shared_ptr<int> lifetime;
    };

    auto parent = system.dispatcher().run(
        "parent", input{output, std::move(lifetime)},
        [](job_context& context, input& data)
        {
            data.output->owner = std::this_thread::get_id();
            auto continuation = context.continue_with("next",
                                                      [](job_context&)
                                                      {
                                                      });
            data.output->notification = context.callbacks().on_complete(
                continuation, "notify continuation", data.output,
                [](job_context&, const task_handle& result, std::shared_ptr<report>& owner)
                {
                    EXPECT_EQ(result.status(), job_status::succeeded);
                    EXPECT_EQ(std::this_thread::get_id(), owner->owner);
                    EXPECT_TRUE(owner->parent_lifetime.expired());
                    ++owner->calls;
                });
        });
    parent.wait();
    output->notification.wait();
    EXPECT_EQ(output->calls, 1);
}

/// @brief Verify independent listeners do not delay the originating job completion.
TEST_F(job_system_fixture, independent_listeners_do_not_delay_the_originating_job_completion)
{
    /// @brief Scenario state for report, isolated per test and released through RAII.
    struct report
    {
        /// @brief Scenario-owned notification used to observe behavior and cleanup without sharing
        /// mutable state between tests.
        task_handle notification;
        /// @brief Scenario-owned registered used to observe behavior and cleanup without sharing
        /// mutable state between tests.
        std::shared_ptr<std::promise<void>> registered;
    } output{{}, std::make_shared<std::promise<void>>()};

    auto registered = output.registered->get_future();

    /// @brief Scenario state for input, isolated per test and released through RAII.
    struct input
    {
        /// @brief Scenario-owned output used to observe behavior and cleanup without sharing
        /// mutable state between tests.
        report* output;
        /// @brief Scenario-owned gate used to observe behavior and cleanup without sharing mutable
        /// state between tests.
        signal_gate* gate;
    };

    auto parent = system.dispatcher().run(
        "parent", input{&output, &gate},
        [](job_context& context, input& data)
        {
            auto independent = context.dispatcher().run("independent", data.gate,
                                                        [](job_context&, signal_gate*& signal)
                                                        {
                                                            signal->wait();
                                                        });
            data.output->notification =
                context.callbacks().on_complete(independent, "notify",
                                                [](job_context&, const task_handle&)
                                                {
                                                });
            data.output->registered->set_value();
        });
    EXPECT_EQ(registered.wait_for(std::chrono::seconds{3}), std::future_status::ready);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
    while (!parent.ready() && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::yield();
    }

    EXPECT_TRUE(parent.ready());
    EXPECT_FALSE(output.notification.ready());
    gate.open();
    parent.wait();
    output.notification.wait();
}

/// @brief Verify independent worker sources allow late registration on the same worker.
TEST(callback_independence_tests,
     independent_worker_sources_allow_late_registration_on_the_same_worker)
{
    job_system system{1};
    auto jobs = system.dispatcher();
    task_handle source;
    task_handle notification;
    jobs.run("creator", &source,
             [](job_context& context, task_handle*& output)
             {
                 *output = context.dispatcher().run("independent",
                                                    [](job_context&)
                                                    {
                                                    });
             })
        .wait();

    /// @brief Scenario state for input, isolated per test and released through RAII.
    struct input
    {
        /// @brief Scenario-owned source used to observe behavior and cleanup without sharing
        /// mutable state between tests.
        task_handle source;
        /// @brief Scenario-owned output used to observe behavior and cleanup without sharing
        /// mutable state between tests.
        task_handle* output;
    };

    jobs.run("registrant", input{source, &notification},
             [](job_context& context, input& data)
             {
                 *data.output = context.callbacks().on_complete(
                     data.source, "late",
                     [](job_context&, const task_handle& result)
                     {
                         EXPECT_EQ(result.status(), job_status::succeeded);
                     });
             })
        .wait();
    notification.wait();
    EXPECT_EQ(system.statistics().submitted, 4U);
}
} // namespace atlas::jobs::tests
