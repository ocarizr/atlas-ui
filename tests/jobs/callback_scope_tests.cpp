#include "job_system_fixture.hpp"

#include <atomic>
#include <thread>

namespace atlas::jobs::tests
{
class callback_scope_tests : public testing::TestWithParam<std::size_t>
{
};

TEST_P(callback_scope_tests, child_notifications_stay_on_the_parent_worker_and_join_its_scope)
{
    job_system system{GetParam()};
    struct results
    {
        int calls{0};
        bool returned{false};
        std::thread::id owner;
    } output;
    struct owned
    {
        std::unique_ptr<int> value;
        results* output;
        std::shared_ptr<int> lifetime;
    };
    auto lifetime = std::make_shared<int>(0);
    std::weak_ptr<int> released = lifetime;
    auto root = system.dispatcher().run(
        "owner", owned{std::make_unique<int>(21), &output, std::move(lifetime)},
        [](job_context& context, owned& data)
        {
            data.output->owner = std::this_thread::get_id();
            auto child = context.spawn("child", [](job_context&) {});
            context.callbacks().on_complete(
                child, "notify", &data,
                [](job_context& inner, const task_handle& result, owned*& owner)
                {
                    EXPECT_EQ(std::this_thread::get_id(), owner->output->owner);
                    EXPECT_TRUE(owner->output->returned);
                    EXPECT_EQ(result.status(), job_status::succeeded);
                    *owner->value *= 2;
                    ++owner->output->calls;
                    inner.spawn("grandchild", owner,
                                [](job_context&, owned*& value) { EXPECT_EQ(*value->value, 42); });
                });
            data.output->returned = true;
        });
    root.wait();
    EXPECT_EQ(output.calls, 1);
    EXPECT_TRUE(released.expired());
    EXPECT_EQ(system.statistics().submitted, system.statistics().completed);
}

INSTANTIATE_TEST_SUITE_P(workers, callback_scope_tests, testing::Values(1U, 4U));

TEST(callback_scope_tests, an_already_completed_child_never_invokes_a_listener_inline)
{
    job_system system{2};
    struct input
    {
        bool returned{false};
        int* calls;
    };
    int calls = 0;
    auto root = system.dispatcher().run(
        "owner", input{false, &calls},
        [](job_context& context, input& data)
        {
            auto child = context.spawn("fast child", [](job_context&) {});
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
            while (!child.ready() && std::chrono::steady_clock::now() < deadline)
            {
                std::this_thread::yield();
            }
            EXPECT_TRUE(child.ready());
            context.callbacks().on_complete(child, "deferred", &data,
                                            [](job_context&, const task_handle&, input*& owner)
                                            {
                                                EXPECT_TRUE(owner->returned);
                                                ++*owner->calls;
                                            });
            EXPECT_EQ(*data.calls, 0);
            data.returned = true;
        });
    root.wait();
    EXPECT_EQ(calls, 1);
}

TEST(callback_scope_tests, cancelled_children_still_deliver_notifications_in_the_parent_scope)
{
    job_system system{1};
    std::stop_source stop;
    bool called = false;
    struct input
    {
        std::stop_source* stop;
        bool* called;
    };
    auto root = system.dispatcher()
                    .with_options({.cancellation = stop.get_token()})
                    .run("owner", input{&stop, &called},
                         [](job_context& context, input& data)
                         {
                             auto child =
                                 context.spawn("cancelled child", [](job_context&) { FAIL(); });
                             context.callbacks().on_cancelled(
                                 child, "cleanup", data.called,
                                 [](job_context& callback, const task_handle& result, bool*& value)
                                 {
                                     EXPECT_FALSE(callback.stop_requested());
                                     EXPECT_EQ(result.status(), job_status::cancelled);
                                     *value = true;
                                 });
                             data.stop->request_stop();
                         });
    EXPECT_THROW(root.wait(), job_error);
    EXPECT_TRUE(called);
    EXPECT_EQ(root.status(), job_status::cancelled);
}

TEST(callback_scope_tests, a_failing_child_observer_does_not_erase_the_original_child_failure)
{
    job_system system{2};
    bool called = false;
    auto root = system.dispatcher().run(
        "owner", &called,
        [](job_context& context, bool*& output)
        {
            auto child = context.spawn("broken child", [](job_context&) { throw 42; });
            context.callbacks().on_failure(child, "observer", output,
                                           [](job_context&, const task_handle& result, bool*& value)
                                           {
                                               EXPECT_THROW(
                                                   std::rethrow_exception(result.exception()), int);
                                               *value = true;
                                               throw std::runtime_error{"listener failure"};
                                           });
        });
    EXPECT_THROW(root.wait(), job_error);
    EXPECT_TRUE(called);
    EXPECT_THROW(std::rethrow_exception(root.exception()), int);
}
} // namespace atlas::jobs::tests
