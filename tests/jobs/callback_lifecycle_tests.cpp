#include "job_system_fixture.hpp"

#include <atomic>
#include <latch>
#include <limits>
#include <thread>

namespace atlas::jobs::tests
{
TEST(callback_lifecycle_tests, bounded_pumping_and_shutdown_deliver_pending_notifications)
{
    job_system system{1};
    auto jobs = system.dispatcher();
    auto source = jobs.run("source", [](job_context&) {});
    source.wait();
    int calls = 0;
    for (int index = 0; index < 3; ++index)
    {
        jobs.callbacks().on_complete(source, "notify", &calls,
                                     [](job_context&, const task_handle&, int*& count)
                                     { ++*count; });
    }
    EXPECT_EQ(system.dispatch_callbacks(0), 0U);
    EXPECT_EQ(system.dispatch_callbacks(1), 1U);
    EXPECT_EQ(calls, 1);
    system.shutdown();
    EXPECT_EQ(calls, 3);
    EXPECT_EQ(system.statistics().caller_dispatched, 3U);
}

TEST_F(job_system_fixture, an_exiting_owner_fails_delivery_without_invoking_on_another_thread)
{
    auto result = std::make_shared<std::promise<task_handle>>();
    auto notification = result->get_future();
    {
        std::jthread owner{[jobs = system.dispatcher(), result, this]
                           {
                               auto source =
                                   jobs.run("held", &gate, [](job_context&, signal_gate*& value)
                                            { value->wait(); });
                               result->set_value(jobs.callbacks().on_complete(
                                   source, "abandoned", [](job_context&, const task_handle&)
                                   { FAIL() << "The originating thread has exited"; }));
                           }};
    }
    gate.open();
    auto handle = notification.get();
    EXPECT_THROW(handle.wait(), job_error);
    EXPECT_EQ(handle.status(), job_status::failed);
    try
    {
        std::rethrow_exception(handle.exception());
    }
    catch (const std::runtime_error& error)
    {
        EXPECT_STREQ(error.what(), "Callback owner thread exited before delivery");
    }
}

TEST(callback_lifecycle_tests, caller_callbacks_have_explicit_trace_identity)
{
    job_system_options options;
    options.trace_capacity = 2;
    job_system system{options};
    auto jobs = system.dispatcher();
    auto source = jobs.run("source", [](job_context&) {});
    auto notification =
        jobs.callbacks().on_complete(source, "notify", [](job_context&, const task_handle&) {});
    notification.wait();
    const auto report = system.trace_report();
    ASSERT_EQ(report.tasks.size(), 2U);
    EXPECT_FALSE(report.tasks[0].caller_thread);
    EXPECT_TRUE(report.tasks[1].caller_thread);
    EXPECT_EQ(report.tasks[1].worker_index, std::numeric_limits<std::size_t>::max());
    EXPECT_EQ(report.tasks[1].dispatch_index, 0U);
    EXPECT_EQ(system.statistics().queued, 0U);
}

TEST(callback_lifecycle_tests, already_queued_notifications_fail_when_the_owner_exits)
{
    job_system system{1};
    std::promise<task_handle> result;
    auto future = result.get_future();
    {
        std::jthread owner{
            [jobs = system.dispatcher(), &result]
            {
                auto source = jobs.run("source", [](job_context&) {});
                source.wait();
                result.set_value(jobs.callbacks().on_complete(
                    source, "undelivered", [](job_context&, const task_handle&) { FAIL(); }));
            }};
    }
    auto notification = future.get();
    EXPECT_THROW(notification.wait(), job_error);
    EXPECT_EQ(system.statistics().queued, 0U);
}

TEST(callback_lifecycle_tests, concurrent_shutdown_callers_each_pump_their_own_notifications)
{
    job_system system{2};
    std::atomic<int> calls{0};
    std::latch ready{2};
    std::latch release{1};
    const auto owner = [&]
    {
        auto jobs = system.dispatcher();
        auto source = jobs.run("source", [](job_context&) {});
        struct input
        {
            std::thread::id owner;
            std::atomic<int>* calls;
        };
        jobs.callbacks().on_complete(source, "notify", input{std::this_thread::get_id(), &calls},
                                     [](job_context&, const task_handle&, input& data)
                                     {
                                         EXPECT_EQ(std::this_thread::get_id(), data.owner);
                                         ++*data.calls;
                                     });
        ready.count_down();
        release.wait();
        system.shutdown();
    };
    {
        std::jthread first{owner};
        std::jthread second{owner};
        ready.wait();
        release.count_down();
    }
    EXPECT_EQ(calls.load(), 2);
}
} // namespace atlas::jobs::tests
