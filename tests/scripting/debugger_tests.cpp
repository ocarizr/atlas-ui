/** @file @brief Embedded Lua debugger protocol and quota contracts. */
#include "debugger_fixture.hpp"

namespace atlas::scripting::tests
{
/// @brief Protected source errors retain the supplied debugger-facing source identity.
TEST(lua_development, PreservesSourceIdentityInErrors)
{
    auto model = std::make_shared<mvvm::view_model>(mvvm::compile({"state"}));
    session behavior{model};
    behavior.load("return {tick=function() error('named failure') end}",
                  "@/project/scripts/ui/menu.lua");
    model->begin_frame(1);
    behavior.begin_frame(1);
    try
    {
        behavior.call("tick");
        FAIL() << "expected failure";
    }
    catch (const std::runtime_error& error)
    {
        EXPECT_NE(std::string{error.what()}.find("project/scripts/ui/menu.lua"), std::string::npos);
    }
}

/// @brief LuaPanda's pure Lua backend attaches under Lua 5.5.1 C++ while application globals stay
/// restricted.
TEST(lua_development, RealAgentAttachesAndDetachesWithoutLeakingCapabilities)
{
    ASSERT_TRUE(session::debugger_available());
    auto model = std::make_shared<mvvm::view_model>(mvvm::compile({"state"}));
    session behavior{model};
    behavior.load("return {tick=function() assert(debug==nil and io==nil and package==nil and "
                  "pcall==nil) end}",
                  "@/project/scripts/ui/menu.lua");
    auto peer = std::make_shared<debug_peer>();
    peer->messages = {
        R"({"cmd":"initSuccess","callbackId":"1","info":{"luaFileExtension":"lua","TempFilePath":"project","cwd":"/project","useCHook":"false","stopOnEntry":"false","autoPathMode":"false","logLevel":"2","adapterVersion":"3.3.0"}}|*|)",
        R"({"cmd":"continue","callbackId":"2","info":{}}|*|)"};
    behavior.attach_debugger({agent_source(), peer, 8818});
    model->begin_frame(1);
    behavior.begin_frame(1);
    EXPECT_NO_THROW(behavior.call("tick"));
    EXPECT_FALSE(peer->sent.empty());
    behavior.detach_debugger();
    EXPECT_TRUE(peer->closed);
}

/// @brief Debugger hooks cannot remove the application instruction quota.
TEST(lua_development, DebugHookCannotDisableQuota)
{
    auto model = std::make_shared<mvvm::view_model>(mvvm::compile({"state"}));
    session_limits limits;
    limits.max_instructions = 1000;
    session behavior{model, limits};
    behavior.load("return {tick=function() while true do end end}");
    const auto agent = "debug.sethook(function() end,'l');return {start=function() end}";
    auto peer = std::make_shared<debug_peer>();
    behavior.attach_debugger({agent, peer, 8818});
    model->begin_frame(1);
    behavior.begin_frame(1);
    EXPECT_THROW(behavior.call("tick"), std::runtime_error);
}

/// @brief Breakpoints expose the mapped call stack and local values in the embedded C++ runtime.
TEST(lua_development, RealAgentBreaksAndInspectsLocals)
{
    auto model = std::make_shared<mvvm::view_model>(mvvm::compile({"state"}));
    session behavior{model};
    behavior.load(
        "return {tick=function()\n local answer=41\n answer=answer+1\n return answer\nend}",
        "@/project/scripts/ui/menu.lua");
    auto peer = std::make_shared<debug_peer>();
    peer->gate = 2;
    peer->messages = {
        R"({"cmd":"initSuccess","callbackId":"1","info":{"luaFileExtension":"lua","TempFilePath":"project","cwd":"/project","useCHook":"false","stopOnEntry":"false","autoPathMode":"false","logLevel":"2","adapterVersion":"3.3.0"}}|*|)",
        R"({"cmd":"setBreakPoint","callbackId":"2","info":{"path":"/project/scripts/ui/menu.lua","bks":[{"line":3,"type":2}]}}|*|)",
        R"({"cmd":"getVariable","callbackId":"3","info":{"varRef":"10000","stackId":"2"}}|*|)",
        R"({"cmd":"getWatchedVariable","callbackId":"4","info":{"varName":"answer","stackId":"2"}}|*|)",
        R"({"cmd":"stopOnStep","callbackId":"5","info":{}}|*|)",
        R"({"cmd":"continue","callbackId":"6","info":{}}|*|)"};
    behavior.attach_debugger({agent_source(), peer, 8818});
    model->begin_frame(1);
    behavior.begin_frame(1);
    EXPECT_NO_THROW(behavior.call("tick"));
    std::string output;
    for (const auto& message : peer->sent)
    {
        output += message;
    }

    EXPECT_NE(output.find("stopOnBreakpoint"), std::string::npos) << output;
    EXPECT_NE(output.find("menu.lua"), std::string::npos) << output;
    EXPECT_NE(output.find("answer"), std::string::npos) << output;
    EXPECT_NE(output.find("41"), std::string::npos) << output;
    EXPECT_NE(output.find("stopOnStep"), std::string::npos) << output;
}
} // namespace atlas::scripting::tests
