/** @file @brief Real shared-interpreter discovery, typed dispatch, reload and category imports. */
#include "callable_fixture.hpp"
#include <limits>

namespace atlas::scripting::tests
{
/// @brief Discover scalar/default metadata without executing user actions or predicates.
TEST_F(callable_test, DiscoversAndInvokesTypedLuaFunctionsOnOneInterpreter)
{
    load(R"(
        actions.register({id='project.gain',label='Gain',category='Progress',
          parameters={{name='amount',type='integer',default=3}},
          execute=function(ctx,amount)ctx.set('xp',ctx.get('xp')+amount)end})
        navigation_conditions.register({id='project.enabled',label='Enabled',category='Progress',
          evaluate=function(ctx)return ctx.get('enabled')end})
        return {}
    )");
    const auto metadata = scripts.callable_descriptors();
    ASSERT_EQ(metadata.size(), 2U);
    EXPECT_EQ(metadata.front().parameters.front().default_value, mvvm::value{std::int64_t{3}});
    const auto action = scripts.bind_callable("project.gain", "hud");
    const auto condition = scripts.bind_callable("project.enabled", "hud");
    begin();
    EXPECT_TRUE(scripts.evaluate_condition(condition));
    scripts.invoke_action(action);
    EXPECT_EQ(model->integer(model->description().field("xp")), 0);
    model->flush();
    EXPECT_EQ(model->integer(model->description().field("xp")), 3);
    EXPECT_EQ(scripts.inspect().interpreters, 1U);
    const std::array<mvvm::value, 1> wrong{3.0};
    EXPECT_THROW(scripts.invoke_action(action, wrong), std::invalid_argument);
    EXPECT_EQ(model->integer(model->description().field("xp")), 3);
}

/// @brief The registry shares its physical interpreter with ordinary UI-domain behaviors.
TEST_F(callable_test, SharesInterpreterWithExistingBehaviorDirector)
{
    scripts.register_source(
        {"behavior", 1, "@project/behavior.lua", "return {tick=function()return 7 end}"});
    const auto behavior = scripts.create({"behavior", "hud"});
    load("navigation_conditions.register({id='project.ok',label='OK',category='General',evaluate="
         "function(ctx)return true end});return {}");
    const auto condition = scripts.bind_callable("project.ok", "hud");
    begin();
    EXPECT_TRUE(scripts.evaluate_condition(condition));
    EXPECT_EQ(scripts.call(behavior, "tick"), (std::vector<mvvm::value>{std::int64_t{7}}));
    EXPECT_EQ(scripts.inspect().interpreters, 1U);
}

/// @brief Failed discovery leaves bindings live; successful reload invalidates old generations.
TEST_F(callable_test, ReloadsAtomicallyAndRejectsOldBindings)
{
    load("navigation_conditions.register({id='project.ok',label='OK',category='General',evaluate="
         "function(ctx)return true end});return {}");
    const auto old = scripts.bind_callable("project.ok", "hud");
    begin();
    EXPECT_THROW(load("error('invalid category')"), std::runtime_error);
    EXPECT_TRUE(scripts.callable_current(old));
    EXPECT_TRUE(scripts.evaluate_condition(old));
    load("navigation_conditions.register({id='project.ok',label='Changed',category='General',"
         "evaluate=function(ctx)return false end});return {}");
    EXPECT_FALSE(scripts.callable_current(old));
    EXPECT_THROW((void)scripts.evaluate_condition(old), std::invalid_argument);
    const auto current = scripts.bind_callable("project.ok", "hud");
    EXPECT_FALSE(scripts.evaluate_condition(current));
}

/// @brief Generated modules are catalog imports and remain entirely project-owned source bytes.
TEST_F(callable_test, GeneratesTypedCategoriesAndEntrypointImports)
{
    callable_descriptor action{
        .id = "project.save",
        .label = "Save",
        .category = "Files",
        .parameters = {{"path", mvvm::value_type::text, mvvm::value{std::string{"a\"b\\c"}}}}};
    callable_descriptor condition{
        .id = "project.ready",
        .label = "Ready",
        .category = "Files",
        .parameters = {{"threshold", mvvm::value_type::number, mvvm::value{0.0}}},
        .kind = callable_kind::condition};
    const std::array definitions{action, condition};
    const auto category = generate_callable_category(definitions);
    EXPECT_NE(category.find("---@param threshold number"), std::string::npos);
    scripts.register_source({"project-category", 1, "@project/script/files.lua", category});
    load(generate_callable_entrypoint(std::array<std::string, 1>{"project-category"}));
    EXPECT_EQ(scripts.callable_descriptors().size(), 2U);
    const auto check = scripts.bind_callable("project.ready", "hud");
    begin();
    EXPECT_FALSE(scripts.evaluate_condition(check));
    scripts.invoke_action(scripts.bind_callable("project.save", "hud"));
}

/// @brief Lua numeric parsing must preserve both signed 64-bit boundaries in generated metadata.
TEST_F(callable_test, PreservesExtremeIntegerDefaultsInGeneratedCategory)
{
    const callable_descriptor action{
        .id = "project.extreme",
        .label = "Extreme",
        .category = "Numbers",
        .parameters = {{"lowest", mvvm::value_type::integer,
                        mvvm::value{std::numeric_limits<std::int64_t>::min()}},
                       {"highest", mvvm::value_type::integer,
                        mvvm::value{std::numeric_limits<std::int64_t>::max()}}}};
    EXPECT_NO_THROW(load(generate_callable_category(std::span{&action, 1})));
    ASSERT_EQ(scripts.callable_descriptors().size(), 1U);
    EXPECT_EQ(scripts.callable_descriptors().front(), action);
}
} // namespace atlas::scripting::tests
