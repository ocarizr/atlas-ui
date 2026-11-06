/** @file @brief Injected logging and profiling report script lifecycle, calls and result phases. */
#include <atlas/scripting/session.hpp>
#include <gtest/gtest.h>
#include <sstream>

namespace atlas::scripting::tests
{
/// @brief Shared profiling policy removes probes while keeping failure diagnostics available.
TEST(script_diagnostics, CapturesOwnerPhasesAndLogsProtectedFailures)
{
    diagnostics::logging_options options;
    options.asynchronous = false;
    auto log = std::make_shared<diagnostics::logger>(options);
    auto profile = std::make_shared<diagnostics::profiler>();
    const diagnostics::services destinations{log, profile};
    auto model = std::make_shared<mvvm::view_model>(mvvm::compile({"state"}));
    session script{model, {}, {}, destinations};
    script.load("return {tick=function() error('diagnostic fault') end}");
    model->begin_frame(1);
    script.begin_frame(1);
    script.deliver_results();
    EXPECT_THROW(script.call("tick"), std::runtime_error);
    EXPECT_GE(log->statistics().written, 1);
    std::ostringstream trace;
    profile->write_chrome_trace(trace);
    for (const auto name : {"script.load", "script.call", "script.results"})
    {
        EXPECT_EQ(trace.str().find(name) != std::string::npos, diagnostics::instrumentation_enabled)
            << name;
    }

    EXPECT_EQ(profile->statistics().errors, 0);
}
} // namespace atlas::scripting::tests
