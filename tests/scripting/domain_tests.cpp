/** @file @brief Shared interpreter instances and independent application-domain lifetimes. */
#include <atlas/scripting/domain.hpp>
#include <gtest/gtest.h>

namespace atlas::scripting::tests
{
/// @brief Instance locals and globals stay isolated while both use the same physical interpreter.
TEST(script_domain, InstancesShareInterpreterWithoutSharingBehaviorState)
{
    auto model = std::make_shared<mvvm::view_model>(mvvm::compile({"state"}));
    domain ui;
    ui.bind_context("hud", model);
    ui.register_source({"counter", 1, "@counter.lua",
                        "local count=0; return {tick=function() count=count+1; return count end}"});
    auto first = ui.create({"counter", "hud"});
    auto second = ui.create({"counter", "hud"});
    model->begin_frame(1);
    ui.begin_frame(1);
    EXPECT_EQ(std::get<std::int64_t>(ui.call(first, "tick").at(0)), 1);
    EXPECT_EQ(std::get<std::int64_t>(ui.call(first, "tick").at(0)), 2);
    EXPECT_EQ(std::get<std::int64_t>(ui.call(second, "tick").at(0)), 1);
    EXPECT_EQ(ui.inspect().interpreters, 1);
    EXPECT_EQ(ui.inspect().instances, 2);
}

/// @brief Closing a UI instance never stops an independently owned application interpreter.
TEST(script_domain, ApplicationDomainOutlivesPresentation)
{
    auto ui_model = std::make_shared<mvvm::view_model>(mvvm::compile({"ui"}));
    auto app_model = std::make_shared<mvvm::view_model>(mvvm::compile({"application"}));
    domain ui, application;
    ui.bind_context("ui", ui_model);
    application.bind_context("state", app_model);
    source_asset source{"counter", 1, "@counter.lua",
                        "local count=0; return {tick=function() count=count+1; return count end}"};
    ui.register_source(source);
    application.register_source(source);
    auto menu = ui.create({"counter", "ui"});
    auto simulation = application.create({"counter", "state"});
    ui_model->begin_frame(1);
    app_model->begin_frame(1);
    ui.begin_frame(1);
    application.begin_frame(1);
    ui.remove(menu);
    EXPECT_THROW(ui.call(menu, "tick"), std::invalid_argument);
    EXPECT_EQ(std::get<std::int64_t>(application.call(simulation, "tick").at(0)), 1);
    auto reopened = ui.create({"counter", "ui"});
    EXPECT_EQ(std::get<std::int64_t>(ui.call(reopened, "tick").at(0)), 1);
    EXPECT_NE(reopened.sequence, menu.sequence);
    EXPECT_THROW(application.call(reopened, "tick"), std::invalid_argument);
}
} // namespace atlas::scripting::tests
