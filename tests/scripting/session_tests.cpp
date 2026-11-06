/** @file @brief Protected Lua calls produce model demands only after successful execution. */
#include <atlas/scripting/session.hpp>
#include <gtest/gtest.h>

namespace atlas::scripting::tests
{
/// @brief Every scalar and both signal namespaces preserve exact data-defined protocols.
TEST(scripting_session, SupportsScalarsActionsAndEventsWithoutNativeSubclasses)
{
    mvvm::definition source{"state",
                            {{"count", std::int64_t{2}},
                             {"ratio", 0.25},
                             {"flag", false},
                             {"title", std::string{"start"}}},
                            {{"gain", {{"amount", mvvm::value_type::integer}}}},
                            {{"toggled", {{"value", mvvm::value_type::boolean}}}}};
    auto model = std::make_shared<mvvm::view_model>(mvvm::compile(source));
    session script{model};
    script.load(R"(
        local count=model.field('count')
        local ratio=model.field('ratio')
        local flag=model.field('flag')
        local title=model.field('title')
        local gain=model.action('gain')
        local toggled=model.event('toggled')
        return {
            tick=function()
                assert(math.type(model.get(count))=='integer')
                assert(model.get(ratio)==0.25 and model.get(flag)==false and model.get(title)=='start')
                model.add_number(ratio,0.5)
                model.set(title,'ready')
                model.invoke(gain,3)
                model.emit(toggled,true)
            end,
            gain=function(amount) model.add_integer(count,amount) end,
            toggled=function(value) model.set(flag,value) end
        }
    )");
    script.watch(model->description().action("gain"), "gain");
    script.watch(model->description().event("toggled"), "toggled");
    model->begin_frame(1);
    script.begin_frame(1);
    script.call("tick");
    model->flush();
    EXPECT_EQ(model->integer(model->description().field("count")), 5);
    EXPECT_DOUBLE_EQ(model->number(model->description().field("ratio")), 0.75);
    EXPECT_TRUE(model->boolean(model->description().field("flag")));
    EXPECT_EQ(model->text(model->description().field("title")), "ready");
}

/// @brief Data-defined fields need no native subclass and remain deferred until model flush.
TEST(scripting_session, ProducesTypedDeferredCommands)
{
    auto model =
        std::make_shared<mvvm::view_model>(mvvm::compile({"state", {{"xp", std::int64_t{2}}}}));
    session script{model};
    script.load("local xp=model.field('xp'); return {tick=function() model.add_integer(xp,3) end}");
    model->begin_frame(1);
    script.begin_frame(1);
    script.call("tick");
    EXPECT_EQ(model->integer(model->description().field("xp")), 2);
    model->flush();
    EXPECT_EQ(model->integer(model->description().field("xp")), 5);
}
} // namespace atlas::scripting::tests
