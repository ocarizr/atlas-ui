#include <atlas/diagnostics/settings.hpp>
#include <gtest/gtest.h>

namespace atlas::diagnostics::tests
{
TEST(settings_tests, yaml_configures_metrics_without_enabling_sampling_or_transport)
{
    const auto settings = parse_settings("metrics: {enabled: false, max_series: 3, "
                                         "max_attributes: 0, max_text_bytes: 32}");
    EXPECT_FALSE(settings.metrics.enabled);
    EXPECT_EQ(settings.metrics.max_series, 3U);
    EXPECT_EQ(settings.metrics.max_attributes, 0U);
    EXPECT_EQ(settings.metrics.max_text_bytes, 32U);
}

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
                                         "logging: {typo: true}", "metrics: {max_series: 0}",
                                         "metrics: {max_attributes: -1}",
                                         "metrics: {max_text_bytes: 0}",
                                         "metrics: {enabled: maybe}", "metrics: {unknown: true}",
                                         "metrics: {enabled: true, enabled: false}"));
} // namespace atlas::diagnostics::tests
