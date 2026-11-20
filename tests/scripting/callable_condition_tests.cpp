/** @file @brief Fail-closed group validation and forbidden condition side effects. */
#include "callable_fixture.hpp"

namespace atlas::scripting::tests
{
/// @brief Negation applies to actual booleans; short circuit preserves declaration order.
TEST_F(callable_test, EvaluatesBoundedGroupsAndRejectsHiddenInvalidLeaves)
{
    std::vector<int> visits;
    scripts.register_condition({"project", callable_origin_kind::project},
                               {.id = "project.yes", .label = "Yes", .category = "General"},
                               [&](const condition_context&, std::span<const mvvm::value>)
                               {
                                   visits.push_back(1);
                                   return true;
                               });
    scripts.register_condition({"project", callable_origin_kind::project},
                               {.id = "project.no", .label = "No", .category = "General"},
                               [&](const condition_context&, std::span<const mvvm::value>)
                               {
                                   visits.push_back(2);
                                   return false;
                               });
    const condition_node yes{scripts.bind_callable("project.yes", "hud")};
    const condition_node no{scripts.bind_callable("project.no", "hud")};
    begin();
    condition_node group{{}, {}, {yes, no, yes}};
    EXPECT_EQ(scripts.evaluate_conditions(group).status, condition_status::blocked);
    EXPECT_EQ(visits, (std::vector<int>{1, 2}));
    group.negate = true;
    EXPECT_TRUE(scripts.evaluate_conditions(group).allowed());
    visits.clear();
    auto unknown = yes;
    unknown.binding.generation += 1;
    unknown.negate = true;
    group.children = {no, unknown};
    EXPECT_EQ(scripts.evaluate_conditions(group).status, condition_status::invalid);
    EXPECT_TRUE(visits.empty());
    EXPECT_FALSE(scripts.evaluate_conditions(condition_node{}).allowed());
}

/// @brief Forbidden operations fail before publication even when a condition is negated.
TEST_F(callable_test, DeniesConditionWritesAndWorkRequests)
{
    load(R"(
        navigation_conditions.register({id='project.bad',label='Bad',category='General',
          evaluate=function(ctx)ctx.set('xp',99);return true end})
        return {}
    )");
    condition_node condition{scripts.bind_callable("project.bad", "hud"), {}, {}, true};
    begin();
    EXPECT_EQ(scripts.evaluate_conditions(condition).status, condition_status::failed);
    model->flush();
    EXPECT_EQ(model->integer(model->description().field("xp")), 0);
    EXPECT_TRUE(scripts.take_callable_requests().empty());
    load(R"(
        navigation_conditions.register({id='project.bad',label='Bad',category='General',
          evaluate=function(ctx)ctx.request('load');return true end})
        return {}
    )");
    condition.binding = scripts.bind_callable("project.bad", "hud");
    EXPECT_EQ(scripts.evaluate_conditions(condition).status, condition_status::failed);
    EXPECT_TRUE(scripts.take_callable_requests().empty());
}

/// @brief A valid Lua truthy value is still invalid for an exact boolean contract.
TEST_F(callable_test, RejectsNonBooleanResultsAndInstructionExhaustion)
{
    load("navigation_conditions.register({id='project.bad',label='Bad',category='General',evaluate="
         "function(ctx)return 1 end});return {}");
    begin();
    condition_node condition{scripts.bind_callable("project.bad", "hud")};
    EXPECT_EQ(scripts.evaluate_conditions(condition).status, condition_status::failed);
    load("navigation_conditions.register({id='project.bad',label='Bad',category='General',evaluate="
         "function(ctx)while true do end end});return {}");
    condition.binding = scripts.bind_callable("project.bad", "hud");
    EXPECT_EQ(scripts.evaluate_conditions(condition).status, condition_status::failed);
    EXPECT_FALSE(scripts.evaluate_conditions(condition).allowed());
}

/// @brief Oversized and deeply nested groups are denied before entering any callback.
TEST_F(callable_test, RejectsExpressionBudgetsBeforeExecution)
{
    int calls = 0;
    scripts.register_condition({"project"},
                               {.id = "project.ok", .label = "OK", .category = "General"},
                               [&](const condition_context&, std::span<const mvvm::value>)
                               {
                                   ++calls;
                                   return true;
                               });
    const condition_node leaf{scripts.bind_callable("project.ok", "hud")};
    begin();
    condition_node wide{{}, {}, std::vector<condition_node>(64, leaf), true};
    EXPECT_FALSE(scripts.evaluate_conditions(wide).allowed());
    auto deep = leaf;
    for (int i = 0; i < 10; ++i)
    {
        deep = condition_node{{}, {}, {std::move(deep)}, true};
    }

    EXPECT_FALSE(scripts.evaluate_conditions(deep).allowed());
    EXPECT_EQ(calls, 0);
}
} // namespace atlas::scripting::tests
