/** @file @brief Action transactions, value-only completions and reload cancellation. */
#include "callable_fixture.hpp"
#include <thread>

namespace atlas::scripting::tests
{
/// @brief Workers can finish owned requests out of order; the owner observes admitted order.
TEST_F(callable_test, DeliversWorkOnOwnerInRequestOrderAndCancelsOldGeneration)
{
    const std::string source =
        R"(actions.register({id='project.work',label='Work',category='Native',
      execute=function(ctx)ctx.request('first',1);ctx.request('second',2)end});return {})";
    load(source);
    auto action = scripts.bind_callable("project.work", "hud");
    begin();
    scripts.invoke_action(action);
    auto requests = scripts.take_callable_requests();
    ASSERT_EQ(requests.size(), 2U);
    EXPECT_TRUE(scripts.take_callable_requests().empty());
    bool completed = false;
    std::jthread worker{
        [&]
        {
            completed = requests.back().work.complete(std::array<mvvm::value, 1>{std::int64_t{9}});
        }};
    worker.join();
    EXPECT_TRUE(completed);
    EXPECT_TRUE(scripts.take_callable_results().empty());
    EXPECT_TRUE(requests.front().work.complete({}, false));
    const auto results = scripts.take_callable_results();
    ASSERT_EQ(results.size(), 2U);
    EXPECT_EQ(results.front().source, action);
    EXPECT_FALSE(results.front().success);
    EXPECT_EQ(results.back().values, (std::vector<mvvm::value>{std::int64_t{9}}));
    EXPECT_FALSE(requests.back().work.complete());
    scripts.invoke_action(action);
    auto stale = scripts.take_callable_requests();
    load(source);
    EXPECT_FALSE(scripts.callable_current(action));
    EXPECT_TRUE(stale.front().work.cancellation.stop_requested());
    EXPECT_FALSE(stale.front().work.complete());
    EXPECT_TRUE(scripts.take_callable_results().empty());
}

/// @brief A callback error cannot leak an earlier queued field write or work request.
TEST_F(callable_test, DiscardsAllDemandsFromFailedLuaAction)
{
    load(R"(actions.register({id='project.bad',label='Bad',category='General',execute=function(ctx)
      ctx.set('xp',9);ctx.request('work');error('stop')end});return {})");
    const auto action = scripts.bind_callable("project.bad", "hud");
    begin();
    EXPECT_THROW(scripts.invoke_action(action), std::runtime_error);
    model->flush();
    EXPECT_EQ(model->integer(model->description().field("xp")), 0);
    EXPECT_TRUE(scripts.take_callable_requests().empty());
    EXPECT_THROW(scripts.invoke_action(action), std::logic_error);
}

/// @brief Registry operations reject foreign threads before entering a shared Lua stack.
TEST_F(callable_test, RejectsWorkerInvocationAndAllowsOnlyValueCompletion)
{
    scripts.register_action({"project"}, {.id = "project.ok", .label = "OK", .category = "General"},
                            [](action_context&, std::span<const mvvm::value>)
                            {
                            });
    const auto action = scripts.bind_callable("project.ok", "hud");
    begin();
    bool rejected = false;
    std::jthread worker{[&]
                        {
                            try
                            {
                                scripts.invoke_action(action);
                            }
                            catch (const std::logic_error&)
                            {
                                rejected = true;
                            }
                        }};
    worker.join();
    EXPECT_TRUE(rejected);
    EXPECT_NO_THROW(scripts.invoke_action(action));
}

/// @brief Schema failure after staging work admits neither the commands nor the work.
TEST_F(callable_test, NativeSchemaFailureRollsBackBothProducerQueues)
{
    scripts.register_action({"project"},
                            {.id = "project.bad", .label = "Bad", .category = "General"},
                            [](action_context& context, std::span<const mvvm::value>)
                            {
                                context.request("work");
                                context.set("xp", true);
                            });
    const auto action = scripts.bind_callable("project.bad", "hud");
    begin();
    EXPECT_THROW(scripts.invoke_action(action), std::invalid_argument);
    EXPECT_TRUE(scripts.take_callable_requests().empty());
    model->flush();
    EXPECT_EQ(model->integer(model->description().field("xp")), 0);
}

/// @brief Admission checks the complete work batch before publishing a staged model assignment.
TEST(CallableBudgets, PendingCapacityFailureDiscardsModelAndWorkDemands)
{
    session_limits limits;
    limits.max_pending_work = 1;
    domain scripts{limits};
    auto model =
        std::make_shared<mvvm::view_model>(mvvm::compile({"state", {{"xp", std::int64_t{0}}}}));
    scripts.bind_context("state", model);
    scripts.register_action({"project"},
                            {.id = "project.bad", .label = "Bad", .category = "General"},
                            [](action_context& context, std::span<const mvvm::value>)
                            {
                                context.set("xp", std::int64_t{9});
                                context.request("one");
                                context.request("two");
                            });
    const auto action = scripts.bind_callable("project.bad", "state");
    model->begin_frame(1);
    scripts.begin_frame(1);
    EXPECT_THROW(scripts.invoke_action(action), std::overflow_error);
    EXPECT_TRUE(scripts.take_callable_requests().empty());
    model->flush();
    EXPECT_EQ(model->integer(model->description().field("xp")), 0);
}
} // namespace atlas::scripting::tests
