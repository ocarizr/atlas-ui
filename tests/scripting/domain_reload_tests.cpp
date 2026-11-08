/** @file @brief Failed frame admission must not publish a replacement behavior generation. */
#include <atlas/scripting/domain.hpp>
#include <gtest/gtest.h>

namespace atlas::scripting::tests
{
/// @brief A failed allocation in a new logical context must not poison existing instances.
TEST(script_domain_reload, SharedLuaBudgetPreservesExistingInstanceAfterFailedCreate)
{
    auto model = std::make_shared<mvvm::view_model>(mvvm::compile({"state"}));
    session_limits limits;
    limits.max_memory_bytes = 65536;
    domain ui{limits};
    ui.bind_context("hud", model);
    ui.register_source({"main", 1, "@main.lua", "return {tick=function()return 7 end}"});
    auto first = ui.create({"main", "hud"});
    bool exhausted = false;
    for (std::size_t i = 0; i < 128; ++i)
    {
        try
        {
            ui.create({"main", "hud"});
        }
        catch (const std::runtime_error&)
        {
            exhausted = true;
            break;
        }
    }

    ASSERT_TRUE(exhausted);
    EXPECT_LE(ui.inspect().lua_bytes, limits.max_memory_bytes);
    model->begin_frame(1);
    ui.begin_frame(1);
    EXPECT_EQ(std::get<std::int64_t>(ui.call(first, "tick").at(0)), 7);
}

/// @brief Invalid execution policies fail before a domain can retain catalog sources.
TEST(script_domain_reload, RejectsInvalidDomainLimits)
{
    session_limits limits;
    limits.max_memory_bytes = 1;
    EXPECT_THROW(domain{limits}, std::invalid_argument);
}

/// @brief Reload remains transactional through frame admission, including native coordinator
/// errors.
TEST(script_domain_reload, FrameAdmissionFailurePreservesOldLuaLocals)
{
    auto model = std::make_shared<mvvm::view_model>(mvvm::compile({"state"}));
    domain ui;
    ui.bind_context("hud", model);
    ui.register_source({"main", 1, "@main.lua",
                        "local value=0;return {tick=function()value=value+1;return value end}"});
    auto instance = ui.create({"main", "hud"});
    model->begin_frame(1);
    ui.begin_frame(1);
    EXPECT_EQ(std::get<std::int64_t>(ui.call(instance, "tick").at(0)), 1);
    model->begin_frame(2);
    EXPECT_THROW(ui.reload(instance), std::logic_error);
    ui.begin_frame(2);
    EXPECT_EQ(std::get<std::int64_t>(ui.call(instance, "tick").at(0)), 2);
}

/// @brief Disabled presentation can reload independently of its inactive model's frame.
TEST(script_domain_reload, DisabledReloadWaitsForActivationFrame)
{
    auto model = std::make_shared<mvvm::view_model>(mvvm::compile({"state"}));
    domain ui;
    ui.bind_context("hud", model);
    ui.register_source({"main", 1, "@main.lua", "return {tick=function()end}"});
    auto instance = ui.create({"main", "hud"});
    model->begin_frame(1);
    ui.begin_frame(1);
    ui.set_enabled(instance, false);
    ui.begin_frame(2);
    instance = ui.reload(instance);
    EXPECT_THROW(ui.set_enabled(instance, true), std::logic_error);
    model->begin_frame(2);
    ui.set_enabled(instance, true);
    EXPECT_NO_THROW(ui.call(instance, "tick"));
}
} // namespace atlas::scripting::tests
