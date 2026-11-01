#include <atlas/diagnostics/metrics.hpp>
#include <gtest/gtest.h>

#include <limits>
#include <thread>
#include <vector>

namespace atlas::diagnostics::tests
{
TEST(metrics_tests, counters_accumulate_and_gauges_replace_without_profile_events)
{
    metrics collection;
    EXPECT_TRUE(collection.increment("menu.visits", 2));
    EXPECT_TRUE(collection.increment("menu.visits"));
    EXPECT_TRUE(collection.set("cpu.percent", 25.5));
    EXPECT_TRUE(collection.set("cpu.percent", 12.5));
    const auto snapshot = collection.snapshot();
    ASSERT_EQ(snapshot.points.size(), 2U);
    EXPECT_EQ(snapshot.points[0].name, "cpu.percent");
    EXPECT_EQ(snapshot.points[0].kind, metric_kind::gauge);
    EXPECT_DOUBLE_EQ(snapshot.points[0].value, 12.5);
    EXPECT_EQ(snapshot.points[1].kind, metric_kind::counter);
    EXPECT_DOUBLE_EQ(snapshot.points[1].value, 3);
    EXPECT_NE(snapshot.started, std::chrono::system_clock::time_point{});
    EXPECT_NE(snapshot.captured, std::chrono::system_clock::time_point{});
    EXPECT_EQ(snapshot.started, collection.snapshot().started);
    EXPECT_EQ(collection.statistics().accepted, 4U);
    EXPECT_EQ(collection.snapshot().points[1].value, 3);
}

TEST(metrics_tests, attributes_are_owned_and_canonicalized)
{
    metrics collection;
    metric_attributes attributes{{"screen", "menu"}, {"mode", "test"}};
    ASSERT_TRUE(collection.increment("visits", 1, attributes));
    attributes[0].second = "other";
    ASSERT_TRUE(collection.increment("visits", 2, {{"mode", "test"}, {"screen", "menu"}}));
    const auto snapshot = collection.snapshot();
    ASSERT_EQ(snapshot.points.size(), 1U);
    EXPECT_EQ(snapshot.points.front().attributes[1].second, "menu");
    EXPECT_DOUBLE_EQ(snapshot.points.front().value, 3);
    EXPECT_TRUE(collection.increment("visits", 1, attributes));
    EXPECT_EQ(snapshot.points.size(), 1U);
    EXPECT_EQ(collection.snapshot().points.size(), 2U);
}

TEST(metrics_tests, type_changes_and_counter_overflow_preserve_previous_values)
{
    metrics collection;
    ASSERT_TRUE(collection.increment("count", std::numeric_limits<double>::max()));
    EXPECT_FALSE(collection.increment("count", std::numeric_limits<double>::max()));
    EXPECT_FALSE(collection.set("count", 0));
    ASSERT_TRUE(collection.set("temperature", -2));
    EXPECT_FALSE(collection.increment("temperature", 1, {{"device", "other"}}));
    const auto snapshot = collection.snapshot();
    ASSERT_EQ(snapshot.points.size(), 2U);
    EXPECT_DOUBLE_EQ(snapshot.points[0].value, std::numeric_limits<double>::max());
    EXPECT_DOUBLE_EQ(snapshot.points[1].value, -2);
    EXPECT_EQ(collection.statistics().rejected, 3U);
}

TEST(metrics_tests, capacity_rejects_new_series_but_existing_series_keep_progressing)
{
    metrics_options options;
    options.max_series = 1;
    metrics collection{options};
    ASSERT_TRUE(collection.increment("count", 1, {{"screen", "menu"}}));
    EXPECT_FALSE(collection.increment("count", 1, {{"screen", "other"}}));
    EXPECT_TRUE(collection.increment("count", 2, {{"screen", "menu"}}));
    EXPECT_EQ(collection.statistics().series, 1U);
    EXPECT_EQ(collection.statistics().dropped, 1U);
    EXPECT_EQ(collection.statistics().rejected, 0U);
    EXPECT_DOUBLE_EQ(collection.snapshot().points.front().value, 3);
}

TEST(metrics_tests, concurrent_increments_and_snapshots_are_consistent)
{
    metrics collection;
    std::vector<std::jthread> producers;
    producers.reserve(4);
    for (int worker = 0; worker < 4; ++worker)
    {
        producers.emplace_back(
            [&collection]
            {
                for (int value = 0; value < 100; ++value)
                {
                    (void)collection.increment("count");
                    (void)collection.snapshot();
                }
            });
    }
    producers.clear();
    EXPECT_DOUBLE_EQ(collection.snapshot().points.front().value, 400);
    EXPECT_EQ(collection.statistics().accepted, 400U);
}
} // namespace atlas::diagnostics::tests
