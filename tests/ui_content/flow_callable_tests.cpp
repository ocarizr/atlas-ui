/** @file @brief Outgoing registry guards remain authoritative until navigation publication. */
#include "flow_runtime_fixture.hpp"
#include <atlas/ui_content/flow_conditions.hpp>

namespace atlas::ui_content::tests
{
/// @brief Ready CPU work uses the shared dispatcher and returns values without touching Lua.
TEST_F(flow_runtime_test, RunsRegisteredActionWorkThroughSharedJobs)
{
    auto model = std::make_shared<mvvm::view_model>(mvvm::compile({"state"}));
    scripting::domain scripts;
    scripts.bind_context("state", model);
    scripts.register_action(
        {"project"}, {.id = "project.work", .label = "Work", .category = "Work"},
        [](scripting::action_context& context, std::span<const mvvm::value>)
        {
            context.request("ready_cpu", std::array<mvvm::value, 1>{std::int64_t{21}});
        });
    const auto action = scripts.bind_callable("project.work", "state");
    model->begin_frame(1);
    scripts.begin_frame(1);
    scripts.invoke_action(action);
    auto work = scripts.take_callable_requests();
    ASSERT_EQ(work.size(), 1U);
    auto payload = std::make_shared<scripting::work_request>(std::move(work.front().work));
    jobs.dispatcher()
        .run("registered ready CPU work", payload,
             [](jobs::job_context&, auto& request)
             {
                 const std::array<mvvm::value, 1> result{
                     std::get<std::int64_t>(request->arguments.front()) * 2};
                 if (!request->complete(result))
                 {
                     throw std::runtime_error("Completion rejected");
                 }
             })
        .wait();
    const auto results = scripts.take_callable_results();
    ASSERT_EQ(results.size(), 1U);
    EXPECT_EQ(results.front().source, action);
    EXPECT_EQ(results.front().values, (std::vector<mvvm::value>{std::int64_t{42}}));
}

/// @brief Changing committed state during loading denies publication and keeps the old screen.
TEST_F(flow_runtime_test, RechecksRegisteredConditionBeforePublishingReadyCandidate)
{
    auto model = std::make_shared<mvvm::view_model>(mvvm::compile({"state", {{"allowed", true}}}));
    scripting::domain scripts;
    scripts.bind_context("state", model);
    scripts.register_condition(
        {"project"}, {.id = "project.allowed", .label = "Allowed", .category = "Flow"},
        [](const scripting::condition_context& context, std::span<const mvvm::value>)
        {
            return std::get<bool>(context.get("allowed"));
        });
    const std::array bindings{
        flow_condition_binding{"open", {scripts.bind_callable("project.allowed", "state")}}};
    model->begin_frame(1);
    scripts.begin_frame(1);
    flow_manager flow{graph(), loader(), {}, make_flow_guard_evaluator(scripts, graph(), bindings)};
    flow.start();
    ASSERT_EQ(flow.advance(2), flow_status::committed);
    gate = load_gate::waiting;
    ASSERT_TRUE(flow.request("open"));
    EXPECT_EQ(flow.advance(3), flow_status::loading);
    model->set(model->description().field("allowed"), false);
    model->flush();
    gate = load_gate::ready;
    EXPECT_EQ(flow.advance(4), flow_status::rejected);
    ASSERT_EQ(flow.presentation().size(), 1U);
    EXPECT_EQ(flow.presentation().front().screen, "home");
}

/// @brief Stale bindings fail closed even when the authored expression negates the predicate.
TEST_F(flow_runtime_test, StaleNegatedGuardFailsWithoutReplacingLiveScreen)
{
    auto model = std::make_shared<mvvm::view_model>(mvvm::compile({"state"}));
    scripting::domain scripts;
    scripts.bind_context("state", model);
    scripts.register_condition({"project"},
                               {.id = "project.allowed", .label = "Allowed", .category = "Flow"},
                               [](const scripting::condition_context&, std::span<const mvvm::value>)
                               {
                                   return false;
                               });
    const std::array bindings{flow_condition_binding{
        "open", {scripts.bind_callable("project.allowed", "state"), {}, {}, true}}};
    model->begin_frame(1);
    scripts.begin_frame(1);
    flow_manager flow{graph(), loader(), {}, make_flow_guard_evaluator(scripts, graph(), bindings)};
    flow.start();
    ASSERT_EQ(flow.advance(2), flow_status::committed);
    scripts.remove_callable("project.allowed");
    ASSERT_TRUE(flow.request("open"));
    EXPECT_EQ(flow.advance(3), flow_status::failed);
    EXPECT_NE(flow.failure(), nullptr);
    EXPECT_EQ(flow.presentation().front().screen, "home");
}
} // namespace atlas::ui_content::tests
