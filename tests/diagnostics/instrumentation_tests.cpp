/** @file @brief Build-controlled optional library instrumentation contracts. */
#include <atlas/diagnostics/instrumentation.hpp>
#include <gtest/gtest.h>
#include <sstream>

namespace atlas::diagnostics::tests
{
/// @brief Isolated synchronous output that can simulate a failing application destination.
class instrumentation_output final : public log_output
{
  public:
    bool fail = false; ///< Injected output failure, configured before observation.
    std::string text;  ///< Recorded operation labels for this case only.

    /// @brief Append synchronously or simulate a failing diagnostic destination.
    void
    append(std::string_view value) override ///< Record or fail without affecting library results.
    {
        if (fail)
        {
            throw std::runtime_error("output failed");
        }

        text.append(value);
    }

    void flush() override
    {
    } ///< No external storage requires flushing.
};

/// @brief Logging stays independent of the build-time profiling policy and contains output errors.
class instrumentation_logging_test : public ::testing::TestWithParam<bool>
{
};

/// @brief Unwinding keeps the original exception even if diagnostic output itself fails.
TEST_P(instrumentation_logging_test, preserves_exception_with_optional_capture_disabled)
{
    auto output = std::make_shared<instrumentation_output>();
    output->fail = GetParam();
    logging_options options;
    options.asynchronous = false;
    auto log = std::make_shared<logger>(options, output);
    services destinations{log, {}};
    EXPECT_THROW((
                     [&]
                     {
                         operation_scope scope{destinations, {"operation.failed", "test.cpu"}};
                         throw std::logic_error("original");
                     }()),
                 std::logic_error);
    if (GetParam())
    {
        EXPECT_GT(destinations.log->statistics().errors, 0U);
    }
    else
    {
        EXPECT_NE(output->text.find("operation.failed"), std::string::npos);
    }
}

INSTANTIATE_TEST_SUITE_P(outputs, instrumentation_logging_test, ::testing::Bool());

/// @brief Enabled probes retain labeled CPU intervals; disabled probes produce no events.
TEST(instrumentation_tests, respects_build_policy_and_explicit_destination)
{
    services destinations{{}, std::make_shared<profiler>()};
    {
        operation_scope scope{destinations, {"test.operation", "test.cpu"}};
    }
    EXPECT_EQ(destinations.profile->statistics().recorded, instrumentation_enabled ? 1U : 0U);
    services absent;
    EXPECT_NO_THROW((operation_scope{absent, {"absent", "test.cpu"}}));
}

/// @brief Unwinding records failure without changing the original exception.
TEST(instrumentation_tests, contains_failures_and_marks_unwinding)
{
    services destinations{{}, std::make_shared<profiler>()};
    EXPECT_THROW((
                     [&]
                     {
                         operation_scope scope{destinations, {"failed.operation", "test.cpu"}};
                         throw std::runtime_error("original failure");
                     }()),
                 std::runtime_error);
    std::ostringstream trace;
    destinations.profile->write_chrome_trace(trace);
    EXPECT_EQ(trace.str().find("failed.operation") != std::string::npos, instrumentation_enabled);
    if (instrumentation_enabled)
    {
        EXPECT_NE(trace.str().find("failed"), std::string::npos);
    }
}
} // namespace atlas::diagnostics::tests
