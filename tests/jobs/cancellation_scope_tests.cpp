#include "job_system_fixture.hpp"

#include <atomic>
#include <stop_token>
#include <vector>

namespace atlas::jobs::tests
{
TEST(cancellation_scope_tests, children_inherit_policy_but_independent_roots_do_not)
{
    job_system system{1};
    std::stop_source stop;
    struct results
    {
        task_handle child;
        task_handle independent;
        task_handle continuation;
        bool ran{false};
    } output;
    struct input
    {
        std::stop_source* stop;
        results* output;
    };
    auto root =
        system.dispatcher()
            .with_options({job_priority::background, stop.get_token()})
            .run("parent", input{&stop, &output},
                 [](job_context& context, input& data)
                 {
                     EXPECT_EQ(context.priority(), job_priority::background);
                     data.output->child = context.spawn("child", [](job_context&) { FAIL(); });
                     data.output->independent = context.dispatcher().run(
                         "independent", data.output,
                         [](job_context& inner, results*& result)
                         {
                             EXPECT_EQ(inner.priority(), job_priority::normal);
                             EXPECT_FALSE(inner.stop_requested());
                             result->ran = true;
                         });
                     data.output->continuation =
                         context.continue_with("continuation", [](job_context&) { FAIL(); });
                     data.stop->request_stop();
                 });
    EXPECT_THROW(root.wait(), job_error);
    output.independent.wait();
    EXPECT_THROW(output.continuation.wait(), job_error);
    EXPECT_TRUE(output.ran);
    EXPECT_EQ(output.child.priority(), job_priority::background);
    EXPECT_EQ(output.child.status(), job_status::cancelled);
    EXPECT_EQ(root.status(), job_status::cancelled);
    EXPECT_EQ(output.continuation.status(), job_status::skipped);
}

TEST(cancellation_scope_tests, cancelled_ranges_suppress_chunks_and_join_their_owner)
{
    job_system system{1};
    std::stop_source stop;
    std::vector<int> values(32);
    struct input
    {
        std::stop_source* stop;
        std::span<int> values;
    };
    auto root = system.dispatcher()
                    .with_options({.cancellation = stop.get_token()})
                    .run("owner", input{&stop, values},
                         [](job_context& context, input& data)
                         {
                             context.parallel_for("range", data.values, 3,
                                                  [](job_context&, std::span<int>&) { FAIL(); });
                             data.stop->request_stop();
                         });
    EXPECT_THROW(root.wait(), job_error);
    EXPECT_EQ(root.status(), job_status::cancelled);
    EXPECT_EQ(values, std::vector<int>(32));
    EXPECT_EQ(system.statistics().submitted, system.statistics().completed);
}

TEST_F(job_system_fixture, cancellation_joins_running_children_before_releasing_owned_input)
{
    auto alive = std::make_shared<int>(42);
    std::weak_ptr<int> lifetime = alive;
    auto started = std::make_shared<std::promise<void>>();
    auto ready = started->get_future();
    struct input
    {
        std::shared_ptr<int> alive;
        signal_gate* gate;
        std::shared_ptr<std::promise<void>> started;
    };
    auto root = system.dispatcher().run(
        "cancelled owner", input{std::move(alive), &gate, started},
        [](job_context& context, input& data)
        {
            struct borrowed
            {
                int* value;
                signal_gate* gate;
                std::shared_ptr<std::promise<void>> started;
            };
            context.spawn("running child", borrowed{data.alive.get(), data.gate, data.started},
                          [](job_context&, borrowed& child)
                          {
                              child.started->set_value();
                              child.gate->wait();
                              EXPECT_EQ(*child.value, 42);
                          });
            throw job_cancelled{};
        });
    EXPECT_EQ(ready.wait_for(std::chrono::seconds{3}), std::future_status::ready);
    EXPECT_FALSE(root.ready());
    EXPECT_FALSE(lifetime.expired());
    gate.open();
    EXPECT_THROW(root.wait(), job_error);
    EXPECT_EQ(root.status(), job_status::cancelled);
    EXPECT_TRUE(lifetime.expired());
}

TEST(cancellation_scope_tests, a_real_child_failure_outranks_parent_or_sibling_cancellation)
{
    job_system system{1};
    auto root = system.dispatcher().run(
        "owner",
        [](job_context& context)
        {
            context.spawn("cancelled child", [](job_context&) { throw job_cancelled{}; });
            context.spawn("broken child", [](job_context&) { throw std::runtime_error{"broken"}; });
            throw job_cancelled{};
        });
    EXPECT_THROW(root.wait(), job_error);
    EXPECT_EQ(root.status(), job_status::failed);
    EXPECT_THROW(std::rethrow_exception(root.exception()), std::runtime_error);
    try
    {
        root.wait();
        FAIL();
    }
    catch (const job_error& error)
    {
        EXPECT_EQ(error.job_name(), "broken child");
    }
}

TEST(cancellation_scope_tests, multiple_cancelled_children_select_the_first_registered_cause)
{
    job_system system{2};
    auto root = system.dispatcher().run(
        "owner",
        [](job_context& context)
        {
            context.spawn("first", [](job_context&) { throw job_cancelled{}; });
            context.spawn("second", [](job_context&) { throw job_cancelled{}; });
        });
    EXPECT_THROW(root.wait(), job_error);
    EXPECT_EQ(root.status(), job_status::cancelled);
    try
    {
        root.wait();
        FAIL();
    }
    catch (const job_error& error)
    {
        EXPECT_EQ(error.job_name(), "first");
    }
}
} // namespace atlas::jobs::tests
