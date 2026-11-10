/** @file @brief Debugger attachment ownership across shared interpreter generations. */
#include "debugger_fixture.hpp"
#include <atlas/scripting/domain.hpp>
#include <sstream>

namespace atlas::scripting::tests
{
/// @brief Reload and stale handles cannot detach a sibling or change another domain's connection.
TEST(lua_debugger_domain, ReloadRetiresOnlyTheSelectedGeneration)
{
    constexpr auto agent = "return {start=function() end}";
    auto model = std::make_shared<mvvm::view_model>(mvvm::compile({"state"}));
    domain ui;
    ui.bind_context("view", model);
    ui.register_source({"menu", 1, "@/project/menu.lua", "return {}"});
    const auto menu = ui.create({"menu", "view"});
    const auto sibling = ui.create({"menu", "view"});
    auto menu_peer = std::make_shared<debug_peer>();
    auto sibling_peer = std::make_shared<debug_peer>();
    ui.attach_debugger(menu, {agent, menu_peer, 8818});
    ui.attach_debugger(sibling, {agent, sibling_peer, 8819});

    domain application;
    EXPECT_THROW(application.detach_debugger(menu), std::invalid_argument);
    ui.register_source({"menu", 2, "@/project/menu.lua", "invalid Lua"});
    EXPECT_THROW(ui.reload(menu), std::runtime_error);
    EXPECT_FALSE(menu_peer->closed);
    EXPECT_FALSE(sibling_peer->closed);

    ui.register_source({"menu", 3, "@/project/menu.lua", "return {}"});
    const auto replacement = ui.reload(menu);
    EXPECT_TRUE(menu_peer->closed);
    EXPECT_FALSE(sibling_peer->closed);
    EXPECT_THROW(ui.detach_debugger(menu), std::invalid_argument);
    auto replacement_peer = std::make_shared<debug_peer>();
    ui.attach_debugger(replacement, {agent, replacement_peer, 8818});
    ui.remove(sibling);
    EXPECT_TRUE(sibling_peer->closed);
    EXPECT_FALSE(replacement_peer->closed);
    ui.shutdown();
    EXPECT_TRUE(replacement_peer->closed);
}

/// @brief A debugger-attached behavior runs authored regions without emitting CPU intervals.
TEST(lua_debugger_domain, AuthoredScopesExcludeDebuggerPauses)
{
    auto model = std::make_shared<mvvm::view_model>(mvvm::compile({"state"}));
    auto profile = std::make_shared<diagnostics::profiler>();
    session script{model, {}, {}, {{}, profile}, {true}};
    script.load(R"(return {tick=function()
        return diagnostic.scope('authored_region', function() return 42 end)
    end})");
    script.attach_debugger({"return {start=function() end}", std::make_shared<debug_peer>()});
    model->begin_frame(1);
    script.begin_frame(1);
    EXPECT_EQ(script.call("tick"), (std::vector<mvvm::value>{std::int64_t{42}}));
    std::ostringstream paused;
    profile->write_chrome_trace(paused);
    EXPECT_EQ(paused.str().find("authored_region"), std::string::npos);
    script.detach_debugger();
    script.call("tick");
    std::ostringstream normal;
    profile->write_chrome_trace(normal);
    EXPECT_EQ(normal.str().find("authored_region") != std::string::npos,
              diagnostics::instrumentation_enabled);
}
} // namespace atlas::scripting::tests
