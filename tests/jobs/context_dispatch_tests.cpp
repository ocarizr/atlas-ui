#include "job_system_fixture.hpp"

#include <atomic>

namespace atlas::jobs::tests
{
TEST_F(job_system_fixture, failed_current_jobs_skip_context_only_continuations)
{
    auto next = std::make_shared<std::promise<task_handle>>();
    auto dispatched = next->get_future();
    auto root = system.dispatcher().run(
        "root", next,
        [](job_context& context, auto& result)
        {
            result->set_value(context.continue_with("skipped", [](job_context&) { FAIL(); }));
            throw 1;
        });
    EXPECT_THROW(root.wait(), job_error);
    auto continuation = dispatched.get();
    EXPECT_THROW(continuation.wait(), job_error);
    EXPECT_EQ(continuation.status(), job_status::skipped);
}

TEST_F(job_system_fixture, an_independent_job_dispatched_from_a_callback_is_not_a_child)
{
    struct input
    {
        signal_gate* gate;
        std::shared_ptr<std::promise<task_handle>> handle;
    };
    auto handle = std::make_shared<std::promise<task_handle>>();
    auto dispatched = handle->get_future();
    auto root = system.dispatcher().run(
        "root", input{&gate, handle},
        [](job_context& context, input& data)
        {
            auto independent = context.dispatcher().run(
                "independent", data.gate, [](job_context&, signal_gate*& held) { held->wait(); });
            data.handle->set_value(std::move(independent));
        });
    EXPECT_EQ(dispatched.wait_for(std::chrono::seconds{3}), std::future_status::ready);
    root.wait();
    auto independent = dispatched.get();
    EXPECT_FALSE(independent.ready());
    gate.open();
    independent.wait();
}

TEST_F(job_system_fixture, a_continuation_waits_for_the_current_job_and_all_its_children)
{
    struct input
    {
        std::shared_ptr<std::atomic<int>> value;
        std::shared_ptr<std::promise<task_handle>> next;
    };
    auto value = std::make_shared<std::atomic<int>>(0);
    auto next = std::make_shared<std::promise<task_handle>>();
    auto dispatched = next->get_future();
    auto root = system.dispatcher().run(
        "root", input{value, next},
        [](job_context& context, input& data)
        {
            context.spawn("child", data.value, [](job_context&, auto& result) { *result = 21; });
            auto continuation =
                context.continue_with("continue", data.value, [](job_context&, auto& result)
                                      { result->store(result->load() * 2); });
            data.next->set_value(std::move(continuation));
        });
    root.wait();
    dispatched.get().wait();
    EXPECT_EQ(value->load(), 42);
}

TEST_F(job_system_fixture, a_context_cannot_be_used_from_another_thread_while_its_body_is_open)
{
    struct input
    {
        signal_gate* gate;
        std::shared_ptr<std::promise<job_context>> context;
    };
    auto saved = std::make_shared<std::promise<job_context>>();
    auto available = saved->get_future();
    auto root = system.dispatcher().run("root", input{&gate, saved},
                                        [](job_context& context, input& data)
                                        {
                                            data.context->set_value(context);
                                            data.gate->wait();
                                        });
    EXPECT_EQ(available.wait_for(std::chrono::seconds{3}), std::future_status::ready);
    auto context = available.get();
    EXPECT_THROW(context.spawn("wrong thread", [](job_context&) {}), std::logic_error);
    EXPECT_THROW((void)context.dispatcher(), std::logic_error);
    EXPECT_THROW(context.continue_with("wrong thread", [](job_context&) {}), std::logic_error);
    gate.open();
    root.wait();
}
} // namespace atlas::jobs::tests
