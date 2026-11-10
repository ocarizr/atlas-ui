/** @file @brief Invalid or excessive authored diagnostics respect owner and destination limits. */
#include <atlas/scripting/domain.hpp>
#include <gtest/gtest.h>

namespace atlas::scripting::tests
{
/// @brief One malformed or excessive authored observation and its expected protected error.
struct diagnostic_case
{
    const char* name;       ///< Readable parameter identity.
    const char* statement;  ///< Behavior body to execute inside the protected handler.
    const char* diagnostic; ///< Expected native error detail.
};

/// @brief Each failed behavior has a fresh context and no admitted metric series.
class authored_diagnostic_limits : public testing::TestWithParam<diagnostic_case>
{
};

/// @brief Bad arguments fail the producer without creating a metric series.
TEST_P(authored_diagnostic_limits, RejectsInvalidObservation)
{
    auto model = std::make_shared<mvvm::view_model>(mvvm::compile({"state"}));
    auto metrics = std::make_shared<diagnostics::metrics>();
    session script{model, {}, {}, {}, {true, metrics}};
    const auto& test = GetParam();
    script.load(std::string{"return {tick=function() "} + test.statement + " end}");
    model->begin_frame(1);
    script.begin_frame(1);
    try
    {
        script.call("tick");
        FAIL() << "expected protected diagnostic error";
    }
    catch (const std::exception& error)
    {
        EXPECT_NE(std::string_view{error.what()}.find(test.diagnostic), std::string_view::npos);
    }

    EXPECT_TRUE(script.inspect().failed);
    EXPECT_EQ(metrics->statistics().series, 0U);
}

INSTANTIATE_TEST_SUITE_P(
    authored_input, authored_diagnostic_limits,
    testing::Values(
        diagnostic_case{"Severity", "diagnostic.log('off','ui','x')", "severity"},
        diagnostic_case{"LongMessage", "diagnostic.log('info','ui',string.rep('x',2049))",
                        "byte limit"},
        diagnostic_case{"BadName", "diagnostic.gauge('bad name',1)", "diagnostic key"},
        diagnostic_case{"NumberType", "diagnostic.gauge('x','42')", "expects name, number"},
        diagnostic_case{"Nonfinite", "diagnostic.gauge('x',math.huge)", "metric value"},
        diagnostic_case{"NegativeCounter", "diagnostic.increment('x',-1)", "metric value"},
        diagnostic_case{"TooManyDimensions",
                        "diagnostic.gauge('x',1,{a='',b='',c='',d='',e='',f='',g='',h='',i=''})",
                        "dimension limit"},
        diagnostic_case{"LongDimension", "diagnostic.gauge('x',1,{mode=string.rep('x',129)})",
                        "byte limit"},
        diagnostic_case{"WrongDimensionType", "diagnostic.gauge('x',1,{mode=true})",
                        "expects a string"}),
    [](const testing::TestParamInfo<diagnostic_case>& info)
    {
        return info.param.name;
    });

/// @brief Rejected kind or cardinality observations remain visible in native registry statistics.
TEST(authored_diagnostics, HonorsNativeKindAndCardinality)
{
    diagnostics::metrics_options limits;
    limits.max_series = 1;
    auto registry = std::make_shared<diagnostics::metrics>(limits);
    auto model = std::make_shared<mvvm::view_model>(mvvm::compile({"state"}));
    session script{model, {}, {}, {}, {true, registry}};
    script.load(R"(return {tick=function()
        assert(diagnostic.increment('opens', 1))
        assert(not diagnostic.gauge('opens', 2))
        assert(not diagnostic.increment('opens', 2, {menu='other'}))
    end})");
    model->begin_frame(1);
    script.begin_frame(1);
    EXPECT_NO_THROW(script.call("tick"));
    EXPECT_EQ(registry->statistics().accepted, 1U);
    EXPECT_EQ(registry->statistics().rejected, 1U);
    EXPECT_EQ(registry->statistics().dropped, 1U);
}

/// @brief One faulty producer cannot flood a sink beyond its shared handler observation budget.
TEST(authored_diagnostics, CapsObservationsPerHandler)
{
    auto registry = std::make_shared<diagnostics::metrics>();
    auto model = std::make_shared<mvvm::view_model>(mvvm::compile({"state"}));
    session script{model, {}, {}, {}, {true, registry}};
    script.load(R"(return {tick=function()
        for i=1,65 do diagnostic.increment('bursts',1) end
    end})");
    model->begin_frame(1);
    script.begin_frame(1);
    EXPECT_THROW(script.call("tick"), std::overflow_error);
    EXPECT_EQ(registry->statistics().accepted, 64U);
}

/// @brief Initialization cannot emit observations even if the host opted in.
TEST(authored_diagnostics, RejectsObservationOutsideHandler)
{
    auto registry = std::make_shared<diagnostics::metrics>();
    auto model = std::make_shared<mvvm::view_model>(mvvm::compile({"state"}));
    session script{model, {}, {}, {}, {true, registry}};
    EXPECT_THROW(script.load("diagnostic.gauge('init', 1); return {}"), std::logic_error);
    EXPECT_EQ(registry->statistics().accepted, 0U);
}
} // namespace atlas::scripting::tests
