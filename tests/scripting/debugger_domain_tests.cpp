/** @file @brief Debugger attachment ownership across shared interpreter generations. */
#include "debugger_fixture.hpp"
#include <atlas/scripting/domain.hpp>

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
} // namespace atlas::scripting::tests
