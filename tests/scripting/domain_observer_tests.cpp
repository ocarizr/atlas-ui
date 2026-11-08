/** @file @brief Script-created listeners apply at safe boundaries and retire with instance scopes.
 */
#include <atlas/scripting/domain.hpp>
#include <gtest/gtest.h>

namespace atlas::scripting::tests
{
/// @brief Registration and removal from Lua never mutate a model's active listener traversal.
TEST(script_domain_observers, ChangesApplyOnlyAtExplicitSynchronization)
{
    auto model = std::make_shared<mvvm::view_model>(
        mvvm::compile({"state", {{"count", std::int64_t{0}}, {"mirror", std::int64_t{0}}}}));
    domain ui;
    ui.bind_context("hud", model);
    ui.register_source({"listener", 1, "@listener.lua", R"(
        local count=model.field('count')
        local mirror=model.field('mirror')
        local token
        return {
            enable=function() token=model.subscribe(count,'changed') end,
            changed=function(value) model.set(mirror,value);model.unsubscribe(token) end
        }
    )"});
    auto instance = ui.create({"listener", "hud"});
    model->begin_frame(1);
    ui.begin_frame(1);
    ui.call(instance, "enable");
    model->set(model->description().field("count"), std::int64_t{1});
    model->flush();
    EXPECT_EQ(model->integer(model->description().field("mirror")), 0);
    ui.synchronize();
    model->set(model->description().field("count"), std::int64_t{2});
    model->flush();
    EXPECT_EQ(model->integer(model->description().field("mirror")), 2);
    ui.synchronize();
    model->set(model->description().field("count"), std::int64_t{3});
    model->flush();
    EXPECT_EQ(model->integer(model->description().field("mirror")), 2);
}

/// @brief Disabling presentation removes scoped listeners and cancels old native requests.
TEST(script_domain_observers, DisableCancelsListenersAndOutstandingWork)
{
    auto model =
        std::make_shared<mvvm::view_model>(mvvm::compile({"state", {{"count", std::int64_t{0}}}}));
    domain ui;
    ui.bind_context("hud", model);
    ui.register_source({"listener", 1, "@listener.lua", R"(
        local count=model.field('count')
        return {enable=function() model.subscribe(count,'changed');model.request('load') end,
                changed=function() error('disabled listener must not run') end}
    )"});
    auto instance = ui.create({"listener", "hud"});
    model->begin_frame(1);
    ui.begin_frame(1);
    ui.call(instance, "enable");
    ui.synchronize();
    auto requests = ui.take_requests();
    ASSERT_EQ(requests.size(), 1);
    ui.set_enabled(instance, false);
    EXPECT_TRUE(requests[0].work.cancellation.stop_requested());
    EXPECT_FALSE(requests[0].work.complete());
    model->set(model->description().field("count"), std::int64_t{1});
    EXPECT_NO_THROW(model->flush());
    EXPECT_THROW(ui.call(instance, "enable"), std::logic_error);
}
} // namespace atlas::scripting::tests
