#include "job_system_fixture.hpp"

#include <optional>
#include <stop_token>
#include <thread>

namespace atlas::jobs::tests
{
TEST_F(job_system_fixture, cancellation_of_pending_work_preserves_prerequisite_lifetimes)
{
    std::stop_source stop;
    auto alive = std::make_shared<int>(42);
    std::weak_ptr<int> lifetime = alive;
    auto held = system.dispatcher().run("prerequisite", &gate,
                                        [](job_context&, signal_gate*& signal) { signal->wait(); });
    auto next = system.dispatcher()
                    .with_options({.cancellation = stop.get_token()})
                    .after(held, "cancelled pending", std::move(alive),
                           [](job_context&, std::shared_ptr<int>&) { FAIL(); });
    stop.request_stop();
    EXPECT_FALSE(next.ready());
    EXPECT_FALSE(lifetime.expired());
    gate.open();
    EXPECT_THROW(next.wait(), job_error);
    EXPECT_EQ(next.status(), job_status::cancelled);
    EXPECT_TRUE(lifetime.expired());
}

TEST_F(job_system_fixture, stop_then_shutdown_drains_cancelled_children_and_releases_the_pool)
{
    std::stop_source stop;
    auto started = std::make_shared<std::promise<void>>();
    auto ready = started->get_future();
    struct input
    {
        signal_gate* gate;
        std::shared_ptr<std::promise<void>> started;
    };
    auto root = system.dispatcher()
                    .with_options({.cancellation = stop.get_token()})
                    .run("running", input{&gate, started},
                         [](job_context& context, input& data)
                         {
                             data.started->set_value();
                             data.gate->wait();
                             context.spawn("cancelled child", [](job_context&) { FAIL(); });
                             context.throw_if_cancelled();
                         });
    EXPECT_EQ(ready.wait_for(std::chrono::seconds{3}), std::future_status::ready);
    stop.request_stop();
    std::jthread stopping{[this] { system.shutdown(); }};
    gate.open();
    stopping.join();
    EXPECT_THROW(root.wait(), job_error);
    EXPECT_EQ(root.status(), job_status::cancelled);
    const auto report = system.statistics();
    EXPECT_EQ(report.submitted, report.completed);
    EXPECT_EQ(report.cancelled, 2U);
    EXPECT_EQ(report.queued + report.running + report.waiting, 0U);
    EXPECT_THROW(system.dispatcher().run("late", [](job_context&) {}), std::logic_error);
}

TEST(cancellation_lifecycle_tests, completed_cancellation_and_policy_outlive_the_pool)
{
    std::stop_source stop;
    stop.request_stop();
    task_handle completed;
    {
        job_system system{1};
        completed = system.dispatcher()
                        .with_options({job_priority::background, stop.get_token()})
                        .run("cancelled", [](job_context&) { FAIL(); });
    }
    EXPECT_TRUE(completed.ready());
    EXPECT_EQ(completed.priority(), job_priority::background);
    EXPECT_THROW(completed.wait(), job_error);
    EXPECT_THROW(std::rethrow_exception(completed.exception()), job_cancelled);
    EXPECT_THROW((void)task_handle{}.priority(), std::invalid_argument);
}

TEST(cancellation_lifecycle_tests, cancellation_observation_requires_the_originating_callback)
{
    job_system system{1};
    std::optional<job_context> escaped;
    system.dispatcher()
        .run("save", &escaped,
             [](job_context& context, std::optional<job_context>*& output)
             {
                 EXPECT_FALSE(context.stop_requested());
                 EXPECT_NO_THROW(context.throw_if_cancelled());
                 output->emplace(context);
             })
        .wait();
    if (escaped.has_value())
    {
        const auto& saved = *escaped;
        EXPECT_THROW((void)saved.stop_requested(), std::logic_error);
        EXPECT_THROW(saved.throw_if_cancelled(), std::logic_error);
        EXPECT_THROW((void)saved.priority(), std::logic_error);
    }
    else
    {
        FAIL() << "The callback did not save its context";
    }
}

#if ATLAS_JOBS_SANITIZER
TEST(cancellation_lifecycle_tests, cancellation_does_not_mask_an_escaped_access_guard)
{
    job_system system{1};
    int value = 0;
    std::optional<access_guard> escaped;
    struct input
    {
        int* value;
        std::optional<access_guard>* escaped;
    };
    auto root = system.dispatcher().run(
        "unsafe cancellation", input{&value, &escaped},
        [](job_context& context, input& data)
        {
            data.escaped->emplace(context.access(write_access(std::span{data.value, 1})));
            throw job_cancelled{};
        });
    EXPECT_THROW(root.wait(), job_error);
    EXPECT_EQ(root.status(), job_status::failed);
    EXPECT_THROW(std::rethrow_exception(root.exception()), job_sanitizer_error);
    escaped.reset();
}
#endif
} // namespace atlas::jobs::tests
