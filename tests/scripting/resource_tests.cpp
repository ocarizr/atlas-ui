/** @file @brief Lua PMR failures and teardown preserve retained-resource ownership. */
#include "counting_resource.hpp"
#include <atlas/scripting/session.hpp>
#include <gtest/gtest.h>

namespace atlas::scripting::tests
{
/// @brief Protected stack unwinding releases all Lua PMR blocks after errors and reloads.
TEST(script_memory, ReclaimsEveryLuaAllocationAcrossFaultsAndReloads)
{
    auto resource = std::make_shared<mvvm::benchmarks::counting_resource>();
    auto model = std::make_shared<mvvm::view_model>(mvvm::compile({"state"}));
    {
        session script{model, {}, resource};
        script.load("return {tick=function() local text=string.rep('x',4096); error(text) end}");
        model->begin_frame(1);
        script.begin_frame(1);
        EXPECT_THROW(script.call("tick"), std::runtime_error);
        EXPECT_GT(resource->live_bytes, 0);
        for (int generation = 0; generation < 5; ++generation)
        {
            script.load("return {}");
            EXPECT_GT(script.inspect().lua_bytes, 0);
            EXPECT_LE(script.inspect().lua_bytes, script.inspect().lua_peak_bytes);
        }
    }
    EXPECT_EQ(resource->live_bytes, 0);
}

/// @brief Candidate VM allocation failure does not retire the current healthy interpreter.
TEST(script_memory, AllocationFailurePreservesOldGenerationAndResourceCleanup)
{
    auto resource = std::make_shared<mvvm::benchmarks::counting_resource>();
    auto model = std::make_shared<mvvm::view_model>(mvvm::compile({"state"}));
    {
        session script{model, {}, resource};
        script.load("return {tick=function() return true end}");
        resource->reject = true;
        EXPECT_THROW(script.load("return {}"), std::bad_alloc);
        resource->reject = false;
        model->begin_frame(1);
        script.begin_frame(1);
        EXPECT_TRUE(std::get<bool>(script.call("tick").at(0)));
    }
    EXPECT_EQ(resource->live_bytes, 0);
}
} // namespace atlas::scripting::tests
