/**
 * @file
 * @brief Behavioral coverage for integration with isolated scenario ownership and cleanup.
 */

#include <atlas/jobs_diagnostics/observer.hpp>
#include <gtest/gtest.h>

#include <sstream>

namespace atlas::jobs_diagnostics::tests
{
/// @brief Verify failed and skipped jobs emit logs and chrome events without changing results.
TEST(integration_tests,
     failed_and_skipped_jobs_emit_logs_and_chrome_events_without_changing_results)
{
    /// @brief Scenario state for output, isolated per test and released through RAII.
    class output final : public diagnostics::log_output
    {
      public:
        /// @brief Record or deliberately fail output as selected by this isolated logging scenario.
        void append(std::string_view text) override
        {
            content.append(text);
        }

        /// @brief Acknowledge or deliberately fail the scenario's controlled output flush.
        void flush() override
        {
        }

        /// @brief Scenario-owned content used to observe behavior and cleanup without sharing
        /// mutable state between tests.
        std::string content;
    };

    auto target = std::make_shared<output>();
    auto log = std::make_shared<diagnostics::logger>(diagnostics::logging_options{}, target);
    auto profile = std::make_shared<diagnostics::profiler>();
    jobs::job_system_options options;
    options.observer = make_observer(log, profile);
    jobs::job_system pool{options};
    auto dispatcher = pool.dispatcher();
    auto failed = dispatcher.run("broken",
                                 [](jobs::job_context&)
                                 {
                                     throw 42;
                                 });
    auto skipped = dispatcher.after(failed, "dependent",
                                    [](jobs::job_context&)
                                    {
                                        FAIL();
                                    });
    pool.shutdown();
    EXPECT_THROW(failed.wait(), jobs::job_error);
    EXPECT_EQ(skipped.status(), jobs::job_status::skipped);
    EXPECT_TRUE(log->flush());
    EXPECT_NE(target->content.find("broken"), std::string::npos);
    EXPECT_NE(target->content.find("failed"), std::string::npos);
    EXPECT_EQ(profile->statistics().recorded, 4U);
    std::ostringstream trace;
    profile->write_chrome_trace(trace);
    EXPECT_NE(trace.str().find("\"result\":\"failed\""), std::string::npos);
    EXPECT_NE(trace.str().find("\"result\":\"skipped\""), std::string::npos);
}
} // namespace atlas::jobs_diagnostics::tests
