/** @file @brief Development attachment rollback, generation retirement and owner isolation. */
#include "debugger_fixture.hpp"
#include <thread>

namespace atlas::scripting::tests
{
/// @brief Missing editor listeners fail attachment explicitly instead of reporting success.
TEST(lua_debugger_lifecycle, UnavailableAdapterRestoresHealthySession)
{
    auto model = std::make_shared<mvvm::view_model>(mvvm::compile({"state"}));
    session behavior{model};
    behavior.load("return {tick=function() end}");
    auto peer = std::make_shared<debug_peer>();
    peer->available = false;
    EXPECT_THROW(behavior.attach_debugger({agent_source(), peer}), std::runtime_error);
    EXPECT_TRUE(peer->closed);
    EXPECT_FALSE(behavior.inspect().failed);
}

/// @brief Invalid agent initialization must restore the restricted healthy application VM.
TEST(lua_debugger_lifecycle, FailedAttachRestoresApplication)
{
    auto model = std::make_shared<mvvm::view_model>(mvvm::compile({"state"}));
    session behavior{model};
    behavior.load("return {tick=function() assert(debug==nil and os==nil and io==nil) end}");
    auto peer = std::make_shared<debug_peer>();
    EXPECT_THROW(behavior.attach_debugger({"error('bad debugger')", peer}), std::runtime_error);
    EXPECT_TRUE(peer->closed);
    EXPECT_FALSE(behavior.inspect().failed);
    model->begin_frame(1);
    behavior.begin_frame(1);
    EXPECT_NO_THROW(behavior.call("tick"));
}

/// @brief Successful reload closes the old attachment; failed replacement preserves it.
TEST(lua_debugger_lifecycle, ReloadRetiresOnlySuccessfulGeneration)
{
    auto model = std::make_shared<mvvm::view_model>(mvvm::compile({"state"}));
    session behavior{model};
    behavior.load("return {tick=function() end}");
    auto peer = std::make_shared<debug_peer>();
    behavior.attach_debugger({"return {start=function() end}", peer});
    EXPECT_THROW(behavior.load("invalid Lua"), std::runtime_error);
    EXPECT_FALSE(peer->closed);
    EXPECT_THROW(behavior.attach_debugger({"return {}", peer}), std::invalid_argument);
    behavior.load("return {tick=function() end}");
    EXPECT_TRUE(peer->closed);
}

/// @brief A source-mapping typo never replaces an already admitted session generation.
TEST(lua_debugger_lifecycle, RejectsInvalidSourceIdentity)
{
    auto model = std::make_shared<mvvm::view_model>(mvvm::compile({"state"}));
    session behavior{model};
    behavior.load("return {tick=function() end}");
    for (const auto* name : {"", "bad\nname", "bad\rname"})
    {
        EXPECT_THROW(behavior.load("return {}", name), std::invalid_argument);
    }

    EXPECT_FALSE(behavior.inspect().failed);
    model->begin_frame(1);
    behavior.begin_frame(1);
    EXPECT_NO_THROW(behavior.call("tick"));
}

/// @brief Transport callbacks are owner work, never invoked by a foreign thread.
TEST(lua_debugger_lifecycle, RejectsForeignAttachmentBeforeTransportAccess)
{
    auto model = std::make_shared<mvvm::view_model>(mvvm::compile({"state"}));
    session behavior{model};
    behavior.load("return {}");
    auto peer = std::make_shared<debug_peer>();
    bool rejected = false;
    std::thread other{[&]
                      {
                          try
                          {
                              behavior.attach_debugger({"return {start=function() end}", peer});
                          }
                          catch (const std::logic_error&)
                          {
                              rejected = true;
                          }
                      }};
    other.join();
    EXPECT_TRUE(rejected);
    EXPECT_FALSE(peer->closed);
    EXPECT_TRUE(peer->sent.empty());
}

/// @brief Removing the optional subscription cannot remove the library's quota hook.
TEST(lua_debugger_lifecycle, RemovingAgentHookKeepsQuota)
{
    auto model = std::make_shared<mvvm::view_model>(mvvm::compile({"state"}));
    session_limits limits;
    limits.max_instructions = 1000;
    session behavior{model, limits};
    behavior.load("return {tick=function() while true do end end}");
    auto peer = std::make_shared<debug_peer>();
    behavior.attach_debugger({"debug.sethook();return {start=function() end}", peer});
    model->begin_frame(1);
    behavior.begin_frame(1);
    EXPECT_THROW(behavior.call("tick"), std::runtime_error);
    EXPECT_TRUE(behavior.inspect().failed);
}
} // namespace atlas::scripting::tests
