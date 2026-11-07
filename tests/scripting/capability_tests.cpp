/** @file @brief The default library exposes no active debugger capability or transport calls. */
#include <atlas/scripting/session.hpp>
#include <gtest/gtest.h>

namespace atlas::scripting::tests
{
/// @brief Development attachment cannot be enabled by a caller of a normal compiled library.
TEST(script_capabilities, DefaultLibraryRejectsDebugger)
{
    EXPECT_FALSE(session::debugger_available());
    auto model = std::make_shared<mvvm::view_model>(mvvm::compile({"state"}));
    session behavior{model};
    behavior.load("return {tick=function() assert(debug==nil) end}");
    EXPECT_THROW(behavior.attach_debugger({"return {}", {}}), std::logic_error);
    EXPECT_FALSE(behavior.inspect().failed);
    model->begin_frame(1);
    behavior.begin_frame(1);
    EXPECT_NO_THROW(behavior.call("tick"));
}
} // namespace atlas::scripting::tests
