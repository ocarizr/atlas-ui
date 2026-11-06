/** @file @brief Optional profiling and bounded field-conflict diagnostics follow shared policy. */
#include <atlas/mvvm/view_model.hpp>
#include <gtest/gtest.h>
#include <sstream>

namespace atlas::mvvm::tests
{
/// @brief Injected destinations collect real operations without enabling probes in release-auto.
TEST(mvvm_diagnostics, CapturesConfiguredScopesAndReportsCoalescingConflicts)
{
    diagnostics::logging_options options;
    options.asynchronous = false;
    auto log = std::make_shared<diagnostics::logger>(options);
    auto profile = std::make_shared<diagnostics::profiler>();
    const diagnostics::services services{log, profile};
    const auto schema = compile({"model", {{"x", std::int64_t{0}}}}, {}, services);
    view_model model{schema, {}, {}, services};
    model.begin_frame(1);
    model.set(schema.field("x"), std::int64_t{1});
    model.set(schema.field("x"), std::int64_t{2});
    model.flush();
    EXPECT_EQ(model.inspect().conflicts, 1);
    EXPECT_GE(log->statistics().written, 1);
    std::ostringstream trace;
    profile->write_chrome_trace(trace);
    for (const auto name : {"mvvm.compile", "mvvm.create", "mvvm.flush"})
    {
        EXPECT_EQ(trace.str().find(name) != std::string::npos, diagnostics::instrumentation_enabled)
            << name;
    }

    EXPECT_EQ(profile->statistics().errors, 0);
}
} // namespace atlas::mvvm::tests
