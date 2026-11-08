/** @file @brief Named model contexts and script listener namespaces remain independent. */
#include <atlas/scripting/domain.hpp>
#include <gtest/gtest.h>

namespace atlas::scripting::tests
{
/// @brief Identical slot numbers in different model instances do not cause callback cross-talk.
TEST(script_domain_contexts, NamedModelsRetainIndependentStateAndCallbacks)
{
    const auto schema = mvvm::compile({"state", {{"count", std::int64_t{0}}}});
    auto first = std::make_shared<mvvm::view_model>(schema);
    auto second = std::make_shared<mvvm::view_model>(schema);
    domain ui;
    ui.bind_context("first", first);
    ui.bind_context("second", second);
    ui.register_source({"main", 1, "@main.lua",
                        "local c=model.field('count');return {tick=function(v)model.set(c,v)end}"});
    auto a = ui.create({"main", "first"});
    auto b = ui.create({"main", "second"});
    first->begin_frame(1);
    second->begin_frame(1);
    ui.begin_frame(1);
    const std::vector<mvvm::value> one{std::int64_t{1}}, two{std::int64_t{2}};
    ui.call(a, "tick", one);
    ui.call(b, "tick", two);
    first->flush();
    second->flush();
    EXPECT_EQ(first->integer(schema.field("count")), 1);
    EXPECT_EQ(second->integer(schema.field("count")), 2);
    EXPECT_EQ(ui.inspect().interpreters, 1);
}

/// @brief All three typed notification namespaces use the same scoped subscription contract.
class domain_signal : public ::testing::TestWithParam<const char*>
{
};

/// @brief Field, action and event subscriptions dispatch to only the selected instance handler.
TEST_P(domain_signal, ScriptSubscribesWithoutNativeViewModelSubclass)
{
    auto model = std::make_shared<mvvm::view_model>(
        mvvm::compile({"state", {{"x", std::int64_t{0}}}, {{"x", {}}}, {{"x", {}}}}));
    domain ui;
    ui.bind_context("hud", model);
    const std::string script = "local x=model." + std::string{GetParam()} +
                               "('x');local visits=0;return "
                               "{enable=function()model.subscribe(x,'changed')end,changed=function("
                               ")visits=visits+1 end,read=function()return visits end}";
    ui.register_source({"main", 1, "@main.lua", script});
    auto instance = ui.create({"main", "hud"});
    model->begin_frame(1);
    ui.begin_frame(1);
    ui.call(instance, "enable");
    ui.synchronize();
    model->set(model->description().field("x"), std::int64_t{1});
    model->invoke(model->description().action("x"));
    model->emit(model->description().event("x"));
    model->flush();
    EXPECT_EQ(std::get<std::int64_t>(ui.call(instance, "read").at(0)), 1);
}

INSTANTIATE_TEST_SUITE_P(namespaces, domain_signal, ::testing::Values("field", "action", "event"));
} // namespace atlas::scripting::tests
