/** @file @brief Contributor identity and exact scalar protocol validation. */
#include "callable_fixture.hpp"

namespace atlas::scripting::tests
{
/// @brief Both implementation kinds reject collisions without publishing a partial module.
TEST_F(callable_test, RejectsForeignNamespaceAndDuplicateNativeLuaIds)
{
    const native_action noop = [](action_context&, std::span<const mvvm::value>)
    {
    };
    EXPECT_THROW(
        scripts.register_action({"project"},
                                {.id = "engine.bad", .label = "Bad", .category = "General"}, noop),
        std::invalid_argument);
    scripts.register_action({"project"}, {.id = "project.ok", .label = "OK", .category = "General"},
                            noop);
    const auto binding = scripts.bind_callable("project.ok", "hud");
    EXPECT_THROW(
        scripts.register_action(
            {"project"}, {.id = "project.ok", .label = "Duplicate", .category = "General"}, noop),
        std::invalid_argument);
    EXPECT_THROW(load("actions.register({id='project.ok',label='Duplicate',category='General',"
                      "execute=function()end});return {}"),
                 std::invalid_argument);
    EXPECT_TRUE(scripts.callable_current(binding));
    EXPECT_EQ(scripts.callable_descriptors().size(), 1U);
    EXPECT_THROW((void)scripts.bind_callable("project.missing", "hud"), std::invalid_argument);
}

/// @brief Choices constrain integers; defaults fill trailing omissions without numeric coercion.
TEST_F(callable_test, EnforcesChoicesDefaultsAndScalarTypesBeforeCallingImplementation)
{
    int calls = 0;
    scripts.register_action(
        {"project"},
        {.id = "project.choose",
         .label = "Choose",
         .category = "General",
         .parameters =
             {{"choice", mvvm::value_type::integer, mvvm::value{std::int64_t{2}}, {{"Two", 2}}}}},
        [&](action_context&, std::span<const mvvm::value> values)
        {
            ++calls;
            EXPECT_EQ(values.front(), mvvm::value{std::int64_t{2}});
        });
    const auto action = scripts.bind_callable("project.choose", "hud");
    begin();
    scripts.invoke_action(action);
    const std::array<mvvm::value, 1> wrong_choice{std::int64_t{3}}, wrong_type{2.0};
    EXPECT_THROW(scripts.invoke_action(action, wrong_choice), std::invalid_argument);
    EXPECT_THROW(scripts.invoke_action(action, wrong_type), std::invalid_argument);
    EXPECT_EQ(calls, 1);
}

/// @brief Generated function parameters must be legal Lua symbols and never shadow the context.
TEST(CallableSchema, RejectsInvalidGeneratedParameterNames)
{
    for (const auto* name : {"end", "context", "bad-name", "1value"})
    {
        const callable_descriptor definition{.id = "project.bad",
                                             .label = "Bad",
                                             .category = "General",
                                             .parameters = {{name, mvvm::value_type::integer}}};
        EXPECT_THROW((void)generate_callable_category(std::span{&definition, 1}),
                     std::invalid_argument)
            << name;
    }
}

/// @brief Reject malformed module metadata without retaining a partial registration.
class callable_invalid_module_test : public callable_test,
                                     public testing::WithParamInterface<const char*>
{
};

/// @brief Structural decoding is exact rather than silently accepting malformed descriptor fields.
TEST_P(callable_invalid_module_test, RejectsDescriptorAndPublishesNothing)
{
    EXPECT_THROW(load(GetParam()), std::invalid_argument);
    EXPECT_TRUE(scripts.callable_descriptors().empty());
}

INSTANTIATE_TEST_SUITE_P(
    InvalidModules, callable_invalid_module_test,
    testing::Values(
        "actions.register({id='project.bad',label='Bad',category='General',typo=true,execute="
        "function()end});return {}",
        "actions.register({id='project.bad',label='Bad',category='General',parameters={{name='x',"
        "type='integer',default=1.0}},execute=function()end});return {}",
        "actions.register({id='project.bad',label='Bad',category='General',parameters={{name='x',"
        "type='integer',default=1},{name='y',type='string'}},execute=function()end});return {}",
        "actions.register({id='project.bad',label='Bad',category='General',parameters={[2]={name='"
        "x',type='integer'}},execute=function()end});return {}",
        "actions.register({id='project.bad',label='Bad',category='General',parameters={{name='x',"
        "type='integer',choices={{label='One',value=1},{label='One',value=2}}}},execute=function()"
        "end});return {}",
        "actions.register({id='foreign.bad',label='Bad',category='General',execute=function()end});"
        "return {}",
        "actions.register({id='project.bad',label='Bad',category='General'});return {}"));
} // namespace atlas::scripting::tests
