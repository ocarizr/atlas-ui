/** @file @brief ViewModel observers use resolved slots and release subscriptions on reload. */
#include <array>
#include <atlas/scripting/session.hpp>
#include <gtest/gtest.h>

namespace atlas::scripting::tests
{
/// @brief Repeated actions preserve deltas; reloading removes old native subscriptions.
TEST(script_observers, ActionsProduceOrderedDeltasAndReloadDetachesObservers)
{
    mvvm::definition data{
        "state", {{"count", std::int64_t{0}}}, {{"gain", {{"amount", mvvm::value_type::integer}}}}};
    auto model = std::make_shared<mvvm::view_model>(mvvm::compile(data));
    session script{model};
    script.load("local c=model.field('count'); return {gain=function(amount) "
                "model.add_integer(c,amount) end}");
    script.watch(model->description().action("gain"), "gain");
    model->begin_frame(1);
    script.begin_frame(1);
    const std::array<mvvm::value, 1> args{std::int64_t{3}};
    model->invoke(model->description().action("gain"), args);
    model->invoke(model->description().action("gain"), args);
    model->flush();
    EXPECT_EQ(model->integer(model->description().field("count")), 6);
    script.load("return {}");
    model->begin_frame(2);
    model->invoke(model->description().action("gain"), args);
    EXPECT_NO_THROW(model->flush());
    EXPECT_EQ(model->integer(model->description().field("count")), 6);
}

/// @brief Observer failure obeys both Lua reload and MVVM explicit reset contracts.
TEST(script_observers, FailingObserverLatchesModelButPreservesCommittedChange)
{
    auto model =
        std::make_shared<mvvm::view_model>(mvvm::compile({"state", {{"count", std::int64_t{0}}}}));
    session script{model};
    script.load("return {changed=function() error('observer failed') end}");
    const auto field = model->description().field("count");
    const auto token = script.watch(field, "changed");
    model->begin_frame(1);
    script.begin_frame(1);
    model->set(field, std::int64_t{1});
    EXPECT_THROW(model->flush(), std::runtime_error);
    EXPECT_EQ(model->integer(field), 1);
    EXPECT_TRUE(model->inspect().failed);
    EXPECT_TRUE(script.inspect().failed);
    script.unwatch(token);
    script.unwatch(token);
    model->reset();
}
} // namespace atlas::scripting::tests
