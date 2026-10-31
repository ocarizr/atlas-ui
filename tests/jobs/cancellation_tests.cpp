#include "job_system_fixture.hpp"

#include <atomic>
#include <stop_token>

namespace atlas::jobs::tests
{
TEST(cancellation_tests, pre_cancelled_work_and_its_dependents_never_invoke_callbacks)
{
    job_system system{1};
    std::stop_source stop;
    EXPECT_TRUE(stop.request_stop());
    EXPECT_FALSE(stop.request_stop());
    auto jobs = system.dispatcher().with_options({.cancellation = stop.get_token()});
    auto root = jobs.run("cancelled", [](job_context&) { FAIL(); });
    auto next = system.dispatcher().after(root, "blocked", [](job_context&) { FAIL(); });
    EXPECT_THROW(root.wait(), job_error);
    EXPECT_THROW(std::rethrow_exception(root.exception()), job_cancelled);
    EXPECT_THROW(next.wait(), job_error);
    EXPECT_EQ(root.status(), job_status::cancelled);
    EXPECT_EQ(next.status(), job_status::skipped);
    EXPECT_EQ(root.timing().execution.count(), 0);
    EXPECT_EQ(system.statistics().cancelled, 1U);
    EXPECT_EQ(system.statistics().failed, 0U);
}

TEST_F(job_system_fixture, running_callbacks_observe_requests_at_explicit_checkpoints)
{
    std::stop_source stop;
    struct input
    {
        signal_gate* gate;
        std::shared_ptr<std::promise<void>> started;
        std::atomic<bool>* forbidden;
    };
    auto started = std::make_shared<std::promise<void>>();
    auto ready = started->get_future();
    std::atomic<bool> forbidden{false};
    auto root = system.dispatcher()
                    .with_options({.cancellation = stop.get_token()})
                    .run("cooperative", input{&gate, started, &forbidden},
                         [](job_context& context, input& data)
                         {
                             EXPECT_FALSE(context.stop_requested());
                             data.started->set_value();
                             data.gate->wait();
                             EXPECT_TRUE(context.stop_requested());
                             context.throw_if_cancelled();
                             data.forbidden->store(true);
                         });
    EXPECT_EQ(ready.wait_for(std::chrono::seconds{3}), std::future_status::ready);
    stop.request_stop();
    gate.open();
    EXPECT_THROW(root.wait(), job_error);
    EXPECT_EQ(root.status(), job_status::cancelled);
    EXPECT_FALSE(forbidden.load());
}

TEST_F(job_system_fixture, normal_return_commits_success_despite_a_late_request)
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
                    .run("commit", input{&gate, started},
                         [](job_context&, input& data)
                         {
                             data.started->set_value();
                             data.gate->wait();
                         });
    EXPECT_EQ(ready.wait_for(std::chrono::seconds{3}), std::future_status::ready);
    stop.request_stop();
    gate.open();
    EXPECT_NO_THROW(root.wait());
    EXPECT_EQ(root.status(), job_status::succeeded);
}
} // namespace atlas::jobs::tests
