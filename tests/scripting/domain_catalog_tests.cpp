/** @file @brief Catalog bytes provide isolated modules and immutable per-instance source snapshots.
 */
#include <atlas/scripting/domain.hpp>
#include <gtest/gtest.h>

namespace atlas::scripting::tests
{
/// @brief Catalog publication cannot silently change an existing instance's unloaded dependencies.
TEST(script_domain_catalog, ModuleRevisionsRemainPinnedUntilReload)
{
    auto model = std::make_shared<mvvm::view_model>(mvvm::compile({"state"}));
    domain ui;
    ui.bind_context("hud", model);
    ui.register_source({"helper", 1, "@helper.lua", "return {value=1}"});
    ui.register_source(
        {"main", 1, "@main.lua", "return {tick=function() return require('helper').value end}"});
    auto first = ui.create({"main", "hud"});
    ui.register_source({"helper", 2, "@helper.lua", "return {value=2}"});
    auto second = ui.create({"main", "hud"});
    model->begin_frame(1);
    ui.begin_frame(1);
    EXPECT_EQ(std::get<std::int64_t>(ui.call(first, "tick").at(0)), 1);
    EXPECT_EQ(std::get<std::int64_t>(ui.call(second, "tick").at(0)), 2);
    auto updated = ui.reload(first);
    EXPECT_THROW(ui.call(first, "tick"), std::invalid_argument);
    EXPECT_EQ(std::get<std::int64_t>(ui.call(updated, "tick").at(0)), 2);
}

/// @brief A module cache belongs to its behavior instance, even inside one physical VM.
TEST(script_domain_catalog, ModulesCannotShareMutableStateBetweenInstances)
{
    auto model = std::make_shared<mvvm::view_model>(mvvm::compile({"state"}));
    domain ui;
    ui.bind_context("hud", model);
    ui.register_source({"helper", 1, "@helper.lua", "return {value=0}"});
    ui.register_source({"main", 1, "@main.lua",
                        "local h=require('helper');return {tick=function() "
                        "h.value=h.value+1;return h.value end}"});
    auto first = ui.create({"main", "hud"});
    auto second = ui.create({"main", "hud"});
    model->begin_frame(1);
    ui.begin_frame(1);
    EXPECT_EQ(std::get<std::int64_t>(ui.call(first, "tick").at(0)), 1);
    EXPECT_EQ(std::get<std::int64_t>(ui.call(first, "tick").at(0)), 2);
    EXPECT_EQ(std::get<std::int64_t>(ui.call(second, "tick").at(0)), 1);
}
} // namespace atlas::scripting::tests
