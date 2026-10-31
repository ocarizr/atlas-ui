#include "job_system_fixture.hpp"

#include <array>
#include <thread>
#include <vector>

namespace atlas::jobs::tests
{
class callback_cleanup_tests : public testing::TestWithParam<bool>
{
};

TEST_P(callback_cleanup_tests, completed_handles_do_not_retain_listener_owned_input)
{
    job_system system{1};
    auto jobs = system.dispatcher();
    auto source = jobs.run("source", [](job_context&) {});
    auto token = std::make_shared<int>(42);
    std::weak_ptr<int> lifetime = token;
    struct input
    {
        std::shared_ptr<int> token;
        bool fail;
    };
    auto notification =
        jobs.callbacks().on_complete(source, "listener", input{std::move(token), GetParam()},
                                     [](job_context&, const task_handle&, input& data)
                                     {
                                         EXPECT_EQ(*data.token, 42);
                                         if (data.fail)
                                         {
                                             throw 42;
                                         }
                                     });
    if (GetParam())
    {
        EXPECT_THROW(notification.wait(), job_error);
    }
    else
    {
        notification.wait();
    }
    EXPECT_TRUE(lifetime.expired());
}

INSTANTIATE_TEST_SUITE_P(outcomes, callback_cleanup_tests, testing::Bool());

TEST(completion_callback_tests, rejected_registration_releases_listener_owned_input)
{
    job_system system{1};
    auto token = std::make_shared<int>(42);
    std::weak_ptr<int> lifetime = token;
    EXPECT_THROW(system.dispatcher().callbacks().on_complete(
                     {}, "invalid", std::move(token),
                     [](job_context&, const task_handle&, std::shared_ptr<int>&) { FAIL(); }),
                 std::invalid_argument);
    EXPECT_TRUE(lifetime.expired());
    EXPECT_EQ(system.statistics().submitted, 0U);
}

TEST(completion_callback_tests, completed_roots_deliver_once_on_the_submitting_thread)
{
    job_system system{2};
    const auto owner = std::this_thread::get_id();
    auto jobs = system.dispatcher();
    auto work = jobs.run("source", [](job_context&) {});
    work.wait();
    struct input
    {
        std::thread::id owner;
        int* count;
    };
    int calls = 0;
    auto notification =
        jobs.callbacks().on_complete(work, "complete", input{owner, &calls},
                                     [](job_context&, const task_handle& result, input& data)
                                     {
                                         EXPECT_EQ(std::this_thread::get_id(), data.owner);
                                         EXPECT_EQ(result.status(), job_status::succeeded);
                                         ++*data.count;
                                     });
    EXPECT_EQ(calls, 0);
    notification.wait();
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(system.dispatch_callbacks(), 0U);
}

TEST(completion_callback_tests, outcome_filters_and_complete_observe_original_results)
{
    job_system system{2};
    auto jobs = system.dispatcher();
    auto callbacks = jobs.callbacks();
    std::array<int, 5> calls{};
    const auto increment = [](job_context&, const task_handle&, int*& count) { ++*count; };
    auto failed = jobs.run("failure", [](job_context&) { throw std::runtime_error{"original"}; });
    auto cancelled = jobs.run("cancelled", [](job_context&) { throw job_cancelled{}; });
    auto skipped = jobs.after(failed, "skipped", [](job_context&) { FAIL(); });
    auto done = callbacks.on_complete(failed, "complete", &calls[0], increment);
    callbacks.on_failure(failed, "failure", &calls[1], increment);
    callbacks.on_cancelled(cancelled, "cancelled", &calls[2], increment);
    callbacks.on_skipped(skipped, "skipped", &calls[3], increment);
    callbacks.on_success(failed, "wrong outcome", &calls[4], increment);
    system.wait_idle();
    EXPECT_EQ(calls, (std::array<int, 5>{1, 1, 1, 1, 0}));
    EXPECT_EQ(done.status(), job_status::succeeded);
    EXPECT_EQ(failed.status(), job_status::failed);
    EXPECT_THROW(std::rethrow_exception(failed.exception()), std::runtime_error);
}

TEST(completion_callback_tests, a_throwing_listener_does_not_suppress_other_listeners)
{
    job_system system{1};
    auto jobs = system.dispatcher();
    auto work = jobs.run("source", [](job_context&) {});
    auto failed = jobs.callbacks().on_complete(work, "broken listener",
                                               [](job_context&, const task_handle&) { throw 42; });
    int calls = 0;
    auto done = jobs.callbacks().on_success(work, "other listener", &calls,
                                            [](job_context&, const task_handle&, int*& result)
                                            { ++*result; });
    done.wait();
    EXPECT_EQ(calls, 1);
    EXPECT_THROW(failed.wait(), job_error);
    EXPECT_THROW(std::rethrow_exception(failed.exception()), int);
    EXPECT_EQ(work.status(), job_status::succeeded);
}

TEST(completion_callback_tests, caller_notifications_join_children_and_nested_caller_listeners)
{
    job_system system{2};
    auto jobs = system.dispatcher();
    auto source = jobs.run("source", [](job_context&) {});
    auto result = std::make_shared<int>(0);
    auto notification = jobs.callbacks().on_complete(
        source, "outer", result,
        [](job_context& context, const task_handle&, std::shared_ptr<int>& value)
        {
            auto child = context.spawn("calculate", value,
                                       [](job_context& inner, std::shared_ptr<int>& output)
                                       {
                                           auto access = inner.access(
                                               write_access(std::span{output.get(), 1}));
                                           *output = 42;
                                       });
            context.callbacks().on_complete(
                child, "inner", value,
                [](job_context& inner, const task_handle&, std::shared_ptr<int>& output)
                {
                    auto access = inner.access(write_access(std::span{output.get(), 1}));
                    ++*output;
                });
        });
    notification.wait();
    EXPECT_EQ(*result, 43);
    EXPECT_EQ(system.statistics().caller_dispatched, 2U);
}
} // namespace atlas::jobs::tests
