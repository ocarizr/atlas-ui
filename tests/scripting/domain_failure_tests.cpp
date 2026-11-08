/** @file @brief Shared domains retain isolation and transactional recovery under failures. */
#include <array>
#include <atlas/scripting/domain.hpp>
#include <gtest/gtest.h>
#include <thread>

namespace atlas::scripting::tests
{
/// @brief Mutable globals and public library tables never affect another instance's environment.
TEST(script_domain_failures, GlobalsAndLibrariesAreIsolated)
{
    auto model = std::make_shared<mvvm::view_model>(mvvm::compile({"state"}));
    domain ui;
    ui.bind_context("hud", model);
    ui.register_source({"main", 1, "@main.lua", R"(
        assert(marker==nil)
        return {poison=function() marker=1;math.abs=nil;string.sub=nil end,
                probe=function() assert(marker==nil and math.abs(-1)==1 and ('text'):sub(1,1)=='t') end}
    )"});
    auto first = ui.create({"main", "hud"});
    auto second = ui.create({"main", "hud"});
    model->begin_frame(1);
    ui.begin_frame(1);
    ui.call(first, "poison");
    EXPECT_NO_THROW(ui.call(second, "probe"));
}

/// @brief A failed reload preserves both the original behavior generation and its native mailbox.
TEST(script_domain_failures, FailedReloadPreservesWorkAndSuccessfulReloadCancelsIt)
{
    auto model =
        std::make_shared<mvvm::view_model>(mvvm::compile({"state", {{"count", std::int64_t{0}}}}));
    domain ui;
    ui.bind_context("hud", model);
    const std::string text = "local count=model.field('count');return "
                             "{tick=function()model.request('load')end,on_result=function(id,ok,v)"
                             "model.set(count,v)end}";
    ui.register_source({"main", 1, "@main.lua", text});
    auto instance = ui.create({"main", "hud"});
    model->begin_frame(1);
    ui.begin_frame(1);
    ui.call(instance, "tick");
    auto work = ui.take_requests();
    ASSERT_EQ(work.size(), 1);
    ui.register_source({"main", 2, "@main.lua", "invalid Lua"});
    EXPECT_THROW(ui.reload(instance), std::runtime_error);
    const std::array<mvvm::value, 1> values{std::int64_t{7}};
    EXPECT_TRUE(work[0].work.complete(values));
    ui.deliver_results();
    model->flush();
    EXPECT_EQ(model->integer(model->description().field("count")), 7);
    ui.call(instance, "tick");
    work = ui.take_requests();
    ASSERT_EQ(work.size(), 1);
    ui.register_source({"main", 3, "@main.lua", text});
    auto replacement = ui.reload(instance);
    EXPECT_TRUE(work[0].work.cancellation.stop_requested());
    EXPECT_FALSE(work[0].work.complete(values));
    EXPECT_THROW(ui.call(instance, "tick"), std::invalid_argument);
    EXPECT_NO_THROW(ui.call(replacement, "tick"));
}

/// @brief Failed producers cannot publish their staged model writes or listener registrations.
TEST(script_domain_failures, ListenerAndModelDemandsRollBackTogether)
{
    auto model =
        std::make_shared<mvvm::view_model>(mvvm::compile({"state", {{"count", std::int64_t{0}}}}));
    domain ui;
    ui.bind_context("hud", model);
    ui.register_source({"main", 1, "@main.lua", R"(
        local count=model.field('count')
        return {tick=function()model.subscribe(count,'changed');model.set(count,99);error('failed producer')end,
                changed=function()error('must never run')end}
    )"});
    auto instance = ui.create({"main", "hud"});
    model->begin_frame(1);
    ui.begin_frame(1);
    EXPECT_THROW(ui.call(instance, "tick"), std::runtime_error);
    EXPECT_EQ(model->inspect().commands, 0);
    ui.set_enabled(instance, false);
    ui.synchronize();
    model->set(model->description().field("count"), std::int64_t{1});
    EXPECT_NO_THROW(model->flush());
}

/// @brief Separate domain lifetimes do not relax the coordinator's fixed thread affinity.
TEST(script_domain_failures, ForeignThreadCannotEnterDomain)
{
    domain ui;
    std::jthread other{[&]
                       {
                           EXPECT_THROW(ui.inspect(), std::logic_error);
                           EXPECT_THROW(ui.begin_frame(1), std::logic_error);
                       }};
    other.join();
    EXPECT_EQ(ui.inspect().instances, 0);
}

/// @brief Imports have no filesystem fallback and cyclic modules cannot recurse until stack
/// exhaustion.
class invalid_domain_module : public ::testing::TestWithParam<const char*>
{
};

/// @brief Each invalid dependency fails safely without admitting an unregistered module.
TEST_P(invalid_domain_module, RejectsMissingCyclicOrValuelessModule)
{
    auto model = std::make_shared<mvvm::view_model>(mvvm::compile({"state"}));
    domain ui;
    ui.bind_context("hud", model);
    ui.register_source({"bad", 1, "@bad.lua", GetParam()});
    ui.register_source({"main", 1, "@main.lua", "return {tick=function() require('bad') end}"});
    auto instance = ui.create({"main", "hud"});
    model->begin_frame(1);
    ui.begin_frame(1);
    EXPECT_THROW(ui.call(instance, "tick"), std::invalid_argument);
    EXPECT_TRUE(ui.inspect(instance).failed);
}

INSTANTIATE_TEST_SUITE_P(catalog, invalid_domain_module,
                         ::testing::Values("return require('bad')",
                                           "return require('unregistered')", "return nil"));
} // namespace atlas::scripting::tests
