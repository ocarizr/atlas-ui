/** @file @brief UI lifecycle and routed input share the same serial behavior technology. */
#include <atlas/ui_scripting/event_handler.hpp>
#include <gtest/gtest.h>

namespace atlas::ui_scripting::tests
{
/// @brief Malformed UI responses must reject the script's commands before model admission.
TEST(ui_script_events, InvalidResponseRollsBackScriptDemands)
{
    auto model =
        std::make_shared<mvvm::view_model>(mvvm::compile({"state", {{"count", std::int64_t{0}}}}));
    auto behavior = std::make_shared<scripting::session>(model);
    behavior->load("local c=model.field('count'); return {pointer_down=function() model.set(c,3); "
                   "return 7 end}");
    model->begin_frame(1);
    behavior->begin_frame(1);
    event_handler handler{behavior};
    ui::command_buffer commands;
    ui::routed_event event{};
    event.kind = ui::event_kind::pointer_down;
    EXPECT_THROW(handler.on_event(event, commands), std::invalid_argument);
    EXPECT_EQ(model->inspect().commands, 0);
}

/// @brief Boolean response flags preserve UI focus/capture/propagation policies.
TEST(ui_script_events, ReturnsTypedInputResponses)
{
    auto model = std::make_shared<mvvm::view_model>(mvvm::compile({"state"}));
    auto behavior = std::make_shared<scripting::session>(model);
    behavior->load("return {pointer_down=function() return false,true,false,true,false end}");
    model->begin_frame(1);
    behavior->begin_frame(1);
    event_handler handler{behavior};
    ui::command_buffer commands;
    ui::routed_event event{};
    event.kind = ui::event_kind::pointer_down;
    const auto response = handler.on_event(event, commands);
    EXPECT_TRUE(response.capture_pointer);
    EXPECT_TRUE(response.focus);
    EXPECT_FALSE(response.stop);
}

/// @brief UI tick produces only deferred ViewModel updates and leaves tree commands empty.
TEST(ui_script_events, DispatchesLifecycleToSharedBehavior)
{
    auto model =
        std::make_shared<mvvm::view_model>(mvvm::compile({"state", {{"count", std::int64_t{0}}}}));
    auto behavior = std::make_shared<scripting::session>(model);
    behavior->load("local count=model.field('count'); return {tick=function(dt) assert(dt==0.5); "
                   "model.add_integer(count,1) end}");
    model->begin_frame(1);
    behavior->begin_frame(1);
    event_handler handler{behavior};
    ui::command_buffer commands;
    commands.begin_frame(1);
    ui::routed_event event{};
    event.kind = ui::event_kind::tick;
    event.delta_seconds = 0.5F;
    handler.on_event(event, commands);
    EXPECT_TRUE(commands.seal().writes().empty());
    model->flush();
    EXPECT_EQ(model->integer(model->description().field("count")), 1);
}
} // namespace atlas::ui_scripting::tests
