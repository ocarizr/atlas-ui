/** @file @brief Script faults, resource limits and reload cannot leak producer side effects. */
#include <atlas/scripting/session.hpp>
#include <gtest/gtest.h>
#include <thread>

namespace atlas::scripting::tests
{
/// @brief Shared numeric authoring model without native behavior subclasses.
class script_failure : public ::testing::Test
{
  protected:
    /// @brief Fresh data-authored model per test, released on the owner.
    std::shared_ptr<mvvm::view_model> model =
        std::make_shared<mvvm::view_model>(mvvm::compile({"state", {{"count", std::int64_t{0}}}}));
    mvvm::field_handle count = model->description().field("count"); ///< Cached numeric slot.
};

/// @brief A failing Lua producer preserves earlier native demands and requires explicit reload.
TEST_F(script_failure, LuaErrorDiscardsOnlyItsOwnStagedCommands)
{
    session script{model};
    script.load("local c=model.field('count'); return {tick=function() model.set(c,9); "
                "model.request('bad'); error('original failure') end}");
    model->begin_frame(1);
    model->set(count, std::int64_t{2});
    script.begin_frame(1);
    try
    {
        script.call("tick");
        FAIL() << "expected protected failure";
    }
    catch (const std::runtime_error& error)
    {
        EXPECT_NE(std::string{error.what()}.find("original failure"), std::string::npos);
    }

    EXPECT_TRUE(script.inspect().failed);
    EXPECT_EQ(script.inspect().pending_work, 0);
    model->flush();
    EXPECT_EQ(model->integer(count), 2);
    EXPECT_THROW(script.call("tick"), std::logic_error);
    script.load("return {tick=function() end}");
    script.begin_frame(1);
    EXPECT_NO_THROW(script.call("tick"));
}

/// @brief Late native type rejection preserves the original exception and old queued values.
TEST_F(script_failure, NativeTypeFailurePreservesOriginalExceptionAndPriorQueue)
{
    session script{model};
    script.load("local c=model.field('count'); return {tick=function() model.set(c,3); "
                "model.set(c,true) end}");
    model->begin_frame(1);
    model->set(count, std::int64_t{1});
    script.begin_frame(1);
    EXPECT_THROW(script.call("tick"), std::invalid_argument);
    model->flush();
    EXPECT_EQ(model->integer(count), 1);
}

/// @brief Infinite script loops fail through the protected instruction quota.
TEST_F(script_failure, InstructionQuotaStopsLoopsWithoutAdmittingCommands)
{
    session_limits limits;
    limits.max_instructions = 1000;
    session script{model, limits};
    script.load("return {tick=function() while true do end end}");
    model->begin_frame(1);
    script.begin_frame(1);
    EXPECT_THROW(script.call("tick"), std::runtime_error);
    EXPECT_EQ(model->inspect().commands, 0);
}

/// @brief Candidate Lua OOM leaves the old interpreter callable.
TEST_F(script_failure, MemoryQuotaProtectsPriorGenerationOnFailedReload)
{
    session_limits limits;
    limits.max_memory_bytes = 65536;
    session script{model, limits};
    script.load("return {tick=function() return 7 end}");
    EXPECT_THROW(script.load("local huge=string.rep('x',1000000); return {}"), std::runtime_error);
    model->begin_frame(1);
    script.begin_frame(1);
    EXPECT_EQ(std::get<std::int64_t>(script.call("tick").at(0)), 7);
}

/// @brief Foreign-thread rejection does not mark the owner's healthy session failed.
TEST_F(script_failure, ForeignOwnerCannotEnterOrMutateTheVm)
{
    session script{model};
    script.load("return {}");
    std::jthread foreign{[&]
                         {
                             EXPECT_THROW(script.call("tick"), std::logic_error);
                         }};
    foreign.join();
    EXPECT_FALSE(script.inspect().failed);
}

/// @brief Scripts cannot open files, install protected quota escapes or load bytecode.
TEST_F(script_failure, RestrictedLibrariesExposeNoStorageOrQuotaEscape)
{
    session script{model};
    EXPECT_NO_THROW(script.load("assert(io==nil and os==nil and package==nil and debug==nil and "
                                "coroutine==nil and load==nil and dofile==nil and pcall==nil and "
                                "xpcall==nil and setmetatable==nil); return {}"));
    EXPECT_THROW(script.load("model.set(model.field('count'),1); return {}"), std::logic_error);
    EXPECT_THROW(script.load(std::string{"\x1bLua", 4}), std::runtime_error);
}
} // namespace atlas::scripting::tests
