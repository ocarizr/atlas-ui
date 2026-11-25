/** @file @brief Lua drag responses are validated before their model demands are admitted. */
#include <atlas/ui_scripting/event_handler.hpp>
#include <gtest/gtest.h>

namespace atlas::ui_scripting::tests
{
/// @brief Offers owned text and receives transfer metadata.
TEST(ui_drag_script, OffersOwnedTextAndReceivesTransferMetadata)
{
    auto model = std::make_shared<mvvm::view_model>(mvvm::compile({"state"}));
    auto behavior = std::make_shared<scripting::session>(model);
    behavior->load("return {drag_prepare=function() return "
                   "false,false,false,false,false,'atlas.asset','stable-id',0,0 end, "
                   "drag_query=function(dt,phase,x,y,key,action,ap,alive,sx,sy,type,text,is_text,"
                   "operation,source,target,ticket,reason) "
                   "assert(type=='atlas.asset' and text=='stable-id' and is_text and operation==1 "
                   "and ticket=='42'); "
                   "return false,false,false,false,false,'','',1,1 end}");
    model->begin_frame(1);
    behavior->begin_frame(1);
    event_handler handler{behavior};
    ui::command_buffer commands;
    ui::routed_event event{};
    event.kind = ui::event_kind::drag_prepare;
    auto response = handler.on_event(event, commands);
    EXPECT_EQ(response.offer.text(), "stable-id");
    event.kind = ui::event_kind::drag_query;
    event.drag = ui::drag_event{.source = {},
                                .target = {},
                                .payload = response.offer,
                                .operation = ui::drag_operation::copy,
                                .transfer = 42};
    response = handler.on_event(event, commands);
    EXPECT_EQ(response.acceptance, ui::drag_acceptance::accept);
    EXPECT_EQ(response.operation, ui::drag_operation::copy);
}

/// @brief Invalid offer rolls back model demands.
TEST(ui_drag_script, InvalidOfferRollsBackModelDemands)
{
    auto model =
        std::make_shared<mvvm::view_model>(mvvm::compile({"state", {{"count", std::int64_t{0}}}}));
    auto behavior = std::make_shared<scripting::session>(model);
    behavior->load("local c=model.field('count'); return {drag_prepare=function() model.set(c,3); "
                   "return false,false,false,false,false,'bad type','data',0,0 end}");
    model->begin_frame(1);
    behavior->begin_frame(1);
    event_handler handler{behavior};
    ui::command_buffer commands;
    ui::routed_event event{};
    event.kind = ui::event_kind::drag_prepare;
    EXPECT_THROW((void)handler.on_event(event, commands), std::invalid_argument);
    EXPECT_EQ(model->inspect().commands, 0);
}
} // namespace atlas::ui_scripting::tests
