/** @file @brief Cross-domain traffic carries only owned scalars and explicit frame identities. */
#include <atlas/scripting/domain.hpp>
#include <gtest/gtest.h>

namespace atlas::scripting::tests
{
/// @brief Typed ingress is exactly sequenced within one frame and revalidates target generation.
TEST(script_domain_messages, DropsRetiredTargetsAndRejectsReplayOrFrameLeakage)
{
    auto model = std::make_shared<mvvm::view_model>(mvvm::compile({"state"}));
    domain ui;
    ui.bind_context("hud", model);
    ui.register_source(
        {"main", 1, "@main.lua",
         "local value=0; return {receive=function(v)value=v end,read=function()return value end}"});
    auto old = ui.create({"main", "hud"});
    model->begin_frame(1);
    ui.begin_frame(1);
    ui.enqueue({old, 1, 1, "receive", {std::int64_t{7}}});
    EXPECT_THROW(ui.enqueue({old, 1, 1, "receive", {std::int64_t{99}}}), std::invalid_argument);
    EXPECT_THROW(ui.enqueue({old, 2, 2, "receive", {std::int64_t{99}}}), std::invalid_argument);
    auto current = ui.reload(old);
    ui.deliver_messages();
    EXPECT_EQ(std::get<std::int64_t>(ui.call(current, "read").at(0)), 0);
    ui.enqueue({current, 2, 1, "receive", {std::int64_t{9}}});
    EXPECT_THROW(ui.begin_frame(2), std::logic_error);
    ui.deliver_messages();
    EXPECT_EQ(std::get<std::int64_t>(ui.call(current, "read").at(0)), 9);
}
} // namespace atlas::scripting::tests
