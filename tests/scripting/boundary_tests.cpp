/** @file @brief Admission, protocol and namespace bounds fail without corrupting prior state. */
#include <array>
#include <atlas/scripting/session.hpp>
#include <gtest/gtest.h>
#include <limits>

namespace atlas::scripting::tests
{
/// @brief A reusable model for independent script-boundary cases.
class script_boundary : public ::testing::Test
{
  protected:
    /// @brief Independent per-test numeric field and action/event namespaces.
    std::shared_ptr<mvvm::view_model> model = std::make_shared<mvvm::view_model>(
        mvvm::compile({"state", {{"count", std::int64_t{0}}}, {{"go", {}}}, {{"done", {}}}}));
};

/// @brief Numeric indices preserve signal namespaces rather than aliasing a same-index field.
TEST_F(script_boundary, RejectsAnActionHandleAsAField)
{
    session script{model};
    script.load(
        "local action=model.action('go'); return {tick=function() model.set(action,2) end}");
    model->begin_frame(1);
    script.begin_frame(1);
    EXPECT_THROW(script.call("tick"), std::invalid_argument);
    EXPECT_EQ(model->inspect().commands, 0);
}

/// @brief The instruction quota cannot be bypassed by a script-installed protected call.
TEST_F(script_boundary, CallQuotaRequiresReloadAfterExhaustion)
{
    session_limits limits;
    limits.max_calls = 1;
    session script{model, limits};
    script.load("return {}");
    model->begin_frame(1);
    script.begin_frame(1);
    EXPECT_TRUE(script.call("optional").empty());
    EXPECT_THROW(script.call("optional"), std::overflow_error);
    EXPECT_TRUE(script.inspect().failed);
}

/// @brief No command from a partially produced call survives its own command quota failure.
TEST_F(script_boundary, CommandQuotaRollsBackTheWholeCall)
{
    session_limits limits;
    limits.max_commands = 1;
    session script{model, limits};
    script.load("local c=model.field('count'); return {tick=function() model.set(c,1); "
                "model.set(c,2) end}");
    model->begin_frame(1);
    script.begin_frame(1);
    EXPECT_THROW(script.call("tick"), std::overflow_error);
    EXPECT_EQ(model->inspect().commands, 0);
}

/// @brief Malformed scalar values do not silently coerce across ViewModel types.
TEST_F(script_boundary, RejectsNonfiniteAndCompositeArguments)
{
    session script{model};
    script.load("return {tick=function() return {} end}");
    model->begin_frame(1);
    script.begin_frame(1);
    const std::array<mvvm::value, 1> invalid{std::numeric_limits<double>::infinity()};
    EXPECT_THROW(script.call("tick", invalid), std::invalid_argument);
    EXPECT_FALSE(script.inspect().failed);
    EXPECT_THROW(script.call("tick"), std::invalid_argument);
}

/// @brief Script load requires returned handlers and respects the source-byte bound.
TEST_F(script_boundary, RejectsInvalidSourceBeforeReplacingGeneration)
{
    session script{model};
    EXPECT_THROW(script.load(""), std::invalid_argument);
    EXPECT_THROW(script.load("return 7"), std::runtime_error);
    EXPECT_THROW(script.load("return {"), std::runtime_error);
    EXPECT_THROW(script.load(std::string(262145, 'x')), std::invalid_argument);
}
} // namespace atlas::scripting::tests
