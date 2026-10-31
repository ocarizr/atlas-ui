#include <atlas/diagnostics/settings.hpp>
#include <gtest/gtest.h>

namespace atlas::diagnostics::tests
{
TEST(settings_tests, yaml_configures_logging_and_profiling_without_opening_files)
{
    const auto settings = parse_settings("logging:\n  level: warning\n  asynchronous: false\n"
                                         "  categories:\n    jobs: debug\n"
                                         "profiling:\n  max_events: 42\n");
    EXPECT_EQ(settings.logging.level, log_level::warning);
    EXPECT_FALSE(settings.logging.asynchronous);
    ASSERT_EQ(settings.logging.category_levels.size(), 1U);
    EXPECT_EQ(settings.logging.category_levels.front().first, "jobs");
    EXPECT_EQ(settings.logging.category_levels.front().second, log_level::debug);
    EXPECT_EQ(settings.profiling.max_events, 42U);
}

class invalid_settings_tests : public testing::TestWithParam<const char*>
{
};
TEST_P(invalid_settings_tests, unknown_keys_types_and_invalid_limits_are_rejected)
{
    EXPECT_THROW((void)parse_settings(GetParam()), std::invalid_argument);
}
INSTANTIATE_TEST_SUITE_P(documents, invalid_settings_tests,
                         testing::Values("logging: {level: loud}", "logging: {queue_capacity: 0}",
                                         "profiling: {max_events: -1}", "unknown: true",
                                         "logging: []", "logging: {asynchronous: maybe}",
                                         "logging: {typo: true}"));
} // namespace atlas::diagnostics::tests
