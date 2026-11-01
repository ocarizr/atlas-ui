/**
 * @file
 * @brief Behavioral coverage for profiling failure with isolated scenario ownership and cleanup.
 */

#include <atlas/diagnostics/profiling.hpp>
#include <gtest/gtest.h>

#include <limits>
#include <sstream>
#include <string>

namespace atlas::diagnostics::tests
{
/// @brief Scenario state for encoding case, isolated per test and released through RAII.
struct encoding_case
{
    /// @brief Scenario-owned name used to observe behavior and cleanup without sharing mutable
    /// state between tests.
    const char* name;
    /// @brief Scenario-owned text used to observe behavior and cleanup without sharing mutable
    /// state between tests.
    std::string text;
    /// @brief Scenario-owned accepted used to observe behavior and cleanup without sharing mutable
    /// state between tests.
    bool accepted;
};

/// @brief Print a readable parameter-case description for GoogleTest failure diagnostics.
std::ostream& operator<<(std::ostream& stream, const encoding_case& value)
{
    return stream << value.name;
}

/// @brief Scenario state for encoding tests, isolated per test and released through RAII.
class encoding_tests : public testing::TestWithParam<encoding_case>
{
};

/// @brief Verify accepts valid utf8 and escapes control characters.
TEST_P(encoding_tests, accepts_valid_utf8_and_escapes_control_characters)
{
    profiler capture;
    const auto& value = GetParam();
    EXPECT_EQ(capture.instant(value.text, "encoding"), value.accepted);
    EXPECT_EQ(capture.statistics().errors, value.accepted ? 0U : 1U);
    if (value.accepted)
    {
        std::ostringstream stream;
        capture.write_chrome_trace(stream);
        if (value.text.front() == '\0')
        {
            EXPECT_NE(stream.str().find("\\u0000"), std::string::npos);
            EXPECT_NE(stream.str().find("\\u0001"), std::string::npos);
        }
        else
        {
            EXPECT_NE(stream.str().find(value.text), std::string::npos);
        }
    }
}

INSTANTIATE_TEST_SUITE_P(encodings, encoding_tests,
                         testing::Values(encoding_case{"Latin", "\xc3\xa9", true},
                                         encoding_case{"Chinese", "\xe7\x95\x8c", true},
                                         encoding_case{"Emoji", "\xf0\x9f\x8e\xae", true},
                                         encoding_case{"Controls", std::string{"\0\n\t\1\\", 5},
                                                       true},
                                         encoding_case{"Truncated", "\xe7\x95", false},
                                         encoding_case{"BadContinuation", "\xe2\x28\xa1", false},
                                         encoding_case{"Overlong", "\xe0\x80\xaf", false},
                                         encoding_case{"Surrogate", "\xed\xa0\x80", false},
                                         encoding_case{"TooLarge", "\xf4\x90\x80\x80", false}),
                         [](const testing::TestParamInfo<encoding_case>& info)
                         {
                             return info.param.name;
                         });

/// @brief Verify invalid utf8 nonfinite counters and string limits are observable.
TEST(profiling_failure_tests, invalid_utf8_nonfinite_counters_and_string_limits_are_observable)
{
    profiling_options options;
    options.max_string_bytes = 8;
    profiler capture{options};
    EXPECT_FALSE(capture.instant(std::string(1, static_cast<char>(0xff)), "test"));
    EXPECT_FALSE(capture.counter("value", "test", std::numeric_limits<double>::infinity()));
    EXPECT_FALSE(capture.instant("long name", "test"));
    EXPECT_EQ(capture.statistics().errors, 2U);
    EXPECT_EQ(capture.statistics().dropped, 1U);
    EXPECT_TRUE(capture.counter("v", "test", 42));
    std::ostringstream stream;
    capture.write_chrome_trace(stream);
    EXPECT_NE(stream.str().find("\"ph\":\"C\""), std::string::npos);
    EXPECT_NE(stream.str().find("\"value\":42"), std::string::npos);
}

/// @brief Verify failed export preserves the capture and disabled capture is empty.
TEST(profiling_failure_tests, failed_export_preserves_the_capture_and_disabled_capture_is_empty)
{
    profiler capture;
    EXPECT_TRUE(capture.instant("retained", "test"));
    std::ostream failed{nullptr};
    EXPECT_THROW(capture.write_chrome_trace(failed), std::runtime_error);
    EXPECT_EQ(capture.statistics().recorded, 1U);
    profiling_options options;
    options.enabled = false;
    profiler disabled{options};
    EXPECT_FALSE(disabled.instant("disabled", "test"));
    EXPECT_EQ(disabled.statistics().recorded, 0U);
}

/// @brief Verify scope owns temporary labels until completion.
TEST(profiling_failure_tests, scope_owns_temporary_labels_until_completion)
{
    profiler capture;
    {
        profile_scope scope{capture, std::string(128, 'x'), std::string(128, 'y')};
    }
    std::ostringstream stream;
    capture.write_chrome_trace(stream);
    EXPECT_NE(stream.str().find(std::string(128, 'x')), std::string::npos);
}
} // namespace atlas::diagnostics::tests
