#include <atlas/diagnostics/metrics.hpp>
#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>

namespace atlas::diagnostics::tests
{
class invalid_metric_tests : public testing::TestWithParam<double>
{
};
TEST_P(invalid_metric_tests, non_finite_or_negative_counter_values_do_not_create_series)
{
    metrics collection;
    EXPECT_FALSE(collection.increment("count", GetParam()));
    EXPECT_TRUE(collection.snapshot().points.empty());
    EXPECT_EQ(collection.statistics().rejected, 1U);
}
INSTANTIATE_TEST_SUITE_P(values, invalid_metric_tests,
                         testing::Values(-1., std::numeric_limits<double>::infinity(),
                                         std::numeric_limits<double>::quiet_NaN()),
                         [](const testing::TestParamInfo<double>& info)
                         {
                             return info.index == 0   ? "negative"
                                    : info.index == 1 ? "infinity"
                                                      : "nan";
                         });

TEST(metrics_failure_tests, zero_attribute_budget_allows_unlabelled_metrics_only)
{
    metrics_options options;
    options.max_attributes = 0;
    metrics collection{options};
    EXPECT_TRUE(collection.increment("count", 0));
    EXPECT_FALSE(collection.increment("count", 1, {{"screen", "menu"}}));
    EXPECT_DOUBLE_EQ(collection.snapshot().points.front().value, 0);
}

TEST(metrics_failure_tests, empty_attribute_values_and_utf8_names_are_supported)
{
    metrics collection;
    EXPECT_TRUE(collection.set("temp.\xc2\xb0", -2, {{"device", ""}}));
    EXPECT_EQ(collection.snapshot().points.front().attributes.front().second, "");
    EXPECT_FALSE(collection.set(std::string{"bad\0name", 8}, 1));
    EXPECT_FALSE(collection.set("bad", 1, {{"key\n", "value"}}));
}

TEST(metrics_failure_tests, invalid_names_attributes_and_limits_are_rejected)
{
    metrics_options options;
    options.max_attributes = 1;
    options.max_text_bytes = 4;
    metrics collection{options};
    EXPECT_FALSE(collection.set("", 1));
    EXPECT_FALSE(collection.set("large", 1));
    EXPECT_FALSE(collection.set("a\nb", 1));
    EXPECT_FALSE(collection.set("a", 1, {{"x", "1"}, {"x", "2"}}));
    EXPECT_FALSE(collection.set("a", 1, {{"", "1"}}));
    EXPECT_FALSE(collection.set("a", 1, {{"x", "large"}}));
    EXPECT_FALSE(collection.set("a", 1, {{"x", std::string{"\xff"}}}));
    EXPECT_FALSE(collection.set("a", std::numeric_limits<double>::infinity()));
    EXPECT_TRUE(collection.snapshot().points.empty());
    options.max_series = 0;
    EXPECT_THROW((void)metrics{options}, std::invalid_argument);
    options.max_series = 1;
    options.max_text_bytes = 0;
    EXPECT_THROW((void)metrics{options}, std::invalid_argument);
}

TEST(metrics_failure_tests, duplicate_attribute_keys_are_rejected_before_registration)
{
    metrics collection;
    EXPECT_FALSE(collection.increment("count", 1, {{"key", "a"}, {"key", "b"}}));
    EXPECT_TRUE(collection.snapshot().points.empty());
    EXPECT_TRUE(collection.set("count", -1, {{"key", "a"}}));
}

TEST(metrics_failure_tests, disabled_collection_is_empty_and_has_no_side_effects)
{
    metrics_options options;
    options.enabled = false;
    metrics collection{options};
    EXPECT_FALSE(collection.increment("count"));
    EXPECT_FALSE(collection.set("cpu", 10));
    EXPECT_TRUE(collection.snapshot().points.empty());
    EXPECT_EQ(collection.statistics().accepted, 0U);
    EXPECT_EQ(collection.statistics().rejected, 0U);
}
} // namespace atlas::diagnostics::tests
