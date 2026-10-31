#include "job_system_fixture.hpp"

#include <thread>

namespace atlas::jobs::tests
{
TEST(callback_validation_tests, invalid_and_foreign_sources_are_rejected_before_admission)
{
    job_system runtime{1};
    job_system foreign{1};
    auto source = foreign.dispatcher().run("foreign", [](job_context&) {});
    const auto forbidden = [](job_context&, const task_handle&) { FAIL(); };
    EXPECT_THROW(runtime.dispatcher().callbacks().on_complete({}, "invalid", forbidden),
                 std::invalid_argument);
    EXPECT_THROW(runtime.dispatcher().callbacks().on_complete(source, "foreign", forbidden),
                 std::invalid_argument);
    EXPECT_EQ(runtime.statistics().submitted, 0U);
}

TEST(callback_validation_tests, registration_from_a_different_thread_is_rejected)
{
    job_system runtime{1};
    auto jobs = runtime.dispatcher();
    auto source = jobs.run("source", [](job_context&) {});
    {
        std::jthread other{[jobs, source]
                           {
                               EXPECT_THROW(jobs.callbacks().on_complete(
                                                source, "wrong thread",
                                                [](job_context&, const task_handle&) { FAIL(); }),
                                            std::logic_error);
                           }};
    }
    source.wait();
    EXPECT_EQ(runtime.statistics().submitted, 1U);
}

TEST(callback_validation_tests, null_callbacks_and_empty_labels_are_rejected)
{
    job_system runtime{1};
    auto jobs = runtime.dispatcher();
    auto source = jobs.run("source", [](job_context&) {});
    using function = void (*)(job_context&, const task_handle&);
    EXPECT_THROW(jobs.callbacks().on_complete(source, "null", static_cast<function>(nullptr)),
                 std::invalid_argument);
    EXPECT_THROW(jobs.callbacks().on_complete(source, "", [](job_context&, const task_handle&) {}),
                 std::invalid_argument);
    EXPECT_EQ(runtime.statistics().submitted, 1U);
}

TEST(callback_validation_tests, callback_facades_do_not_keep_destroyed_pools_alive)
{
    task_handle source;
    auto callbacks = [&]
    {
        job_system runtime{1};
        source = runtime.dispatcher().run("source", [](job_context&) {});
        return runtime.dispatcher().callbacks();
    }();
    EXPECT_THROW(callbacks.on_complete(source, "late", [](job_context&, const task_handle&) {}),
                 std::logic_error);
}

TEST(callback_validation_tests, callbacks_cannot_reenter_waits_pumping_or_shutdown)
{
    job_system runtime{1};
    auto jobs = runtime.dispatcher();
    auto source = jobs.run("source", [](job_context&) {});
    auto notification = jobs.callbacks().on_complete(
        source, "reentrancy", &runtime,
        [](job_context&, const task_handle& result, job_system*& pool)
        {
            EXPECT_THROW(result.wait(), std::logic_error);
            EXPECT_THROW(pool->wait_idle(), std::logic_error);
            EXPECT_THROW(pool->shutdown(), std::logic_error);
            EXPECT_THROW((void)pool->dispatch_callbacks(), std::logic_error);
        });
    notification.wait();
}

TEST(callback_validation_tests, worker_callbacks_cannot_resurrect_an_unrelated_completed_scope)
{
    job_system runtime{1};
    task_handle child;
    auto jobs = runtime.dispatcher();
    jobs.run("first parent", &child, [](job_context& context, task_handle*& output)
             { *output = context.spawn("child", [](job_context&) {}); })
        .wait();
    auto next = jobs.run(
        "other parent", child,
        [](job_context& context, task_handle& source)
        {
            EXPECT_THROW(context.callbacks().on_complete(
                             source, "late", [](job_context&, const task_handle&) { FAIL(); }),
                         std::logic_error);
        });
    next.wait();
    EXPECT_EQ(runtime.statistics().submitted, 3U);
}

TEST(callback_validation_tests, invalid_events_and_registration_after_shutdown_are_rejected)
{
    job_system runtime{1};
    auto jobs = runtime.dispatcher();
    auto source = jobs.run("source", [](job_context&) {});
    // Deliberately construct invalid event input to exercise the public guard.
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    const auto invalid = static_cast<completion_event>(5);
    EXPECT_THROW(jobs.callbacks().on(invalid, source, "invalid event",
                                     [](job_context&, const task_handle&) {}),
                 std::invalid_argument);
    runtime.shutdown();
    EXPECT_THROW(
        jobs.callbacks().on_complete(source, "late", [](job_context&, const task_handle&) {}),
        std::logic_error);
    EXPECT_EQ(runtime.statistics().submitted, 1U);
}

TEST(callback_validation_tests, self_completion_observers_are_independent_continuations)
{
    job_system runtime{1};
    auto jobs = runtime.dispatcher();
    auto source = jobs.run("source", [](job_context&) {});
    auto self = std::make_shared<task_handle>();
    auto next = std::make_shared<task_handle>();
    int calls = 0;
    struct input
    {
        std::shared_ptr<task_handle> self;
        std::shared_ptr<task_handle> next;
        int* calls;
    };
    *self = jobs.callbacks().on_complete(
        source, "self", input{self, next, &calls},
        [](job_context& context, const task_handle&, input& data)
        {
            *data.next = context.callbacks().on_complete(
                *data.self, "next", data.calls,
                [](job_context&, const task_handle& result, int*& value)
                {
                    EXPECT_EQ(result.status(), job_status::succeeded);
                    ++*value;
                });
        });
    self->wait();
    next->wait();
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(runtime.statistics().submitted, 3U);
}
} // namespace atlas::jobs::tests
