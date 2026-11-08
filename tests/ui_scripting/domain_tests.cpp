/** @file @brief UI adapters target generation-validated instances in a shared Lua domain. */
#include <atlas/ui_scripting/event_handler.hpp>
#include <gtest/gtest.h>

namespace atlas::ui_scripting::tests
{
/// @brief Widgets forward to isolated behaviors without constructing a VM per widget.
TEST(ui_script_domains, RoutesToSelectedInstanceAndRejectsRetiredHandle)
{
    auto model =
        std::make_shared<mvvm::view_model>(mvvm::compile({"state", {{"count", std::int64_t{0}}}}));
    auto runtime = std::make_shared<scripting::domain>();
    runtime->bind_context("hud", model);
    runtime->register_source(
        {"button", 1, "@button.lua",
         "local c=model.field('count');return "
         "{pointer_down=function()model.add_integer(c,1);return false,true end}"});
    auto instance = runtime->create({"button", "hud"});
    model->begin_frame(1);
    runtime->begin_frame(1);
    event_handler handler{runtime, instance};
    ui::command_buffer commands;
    ui::routed_event event{};
    event.kind = ui::event_kind::pointer_down;
    EXPECT_TRUE(handler.on_event(event, commands).capture_pointer);
    model->flush();
    EXPECT_EQ(model->integer(model->description().field("count")), 1);
    runtime->remove(instance);
    EXPECT_THROW(handler.on_event(event, commands), std::invalid_argument);
}
} // namespace atlas::ui_scripting::tests
