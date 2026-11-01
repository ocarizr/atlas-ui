/**
 * @file
 * @brief Behavioral coverage for metrics reporting with isolated scenario ownership and cleanup.
 */

#include <atlas/diagnostics/metrics.hpp>
#include <gtest/gtest.h>

#include <stdexcept>

namespace atlas::diagnostics::tests
{
namespace
{
/// @brief Scenario state for recording output, isolated per test and released through RAII.
class recording_output final : public metrics_output
{
  public:
    /// @brief Copy the owning metric snapshot and apply the controlled reporting scenario.
    void publish(const metric_snapshot& snapshot) override
    {
        // Exporters run outside the registry lock and may retain an owning copy.
        saved = snapshot;
        ++calls;
        if (source)
        {
            (void)source->increment("during.export");
        }

        if (fail)
        {
            if (standard_failure)
            {
                throw std::runtime_error{"transport unavailable"};
            }

            throw 42;
        }
    }

    /// @brief Scenario-owned saved used to observe behavior and cleanup without sharing mutable
    /// state between tests.
    metric_snapshot saved;
    /// @brief Scenario-owned source used to observe behavior and cleanup without sharing mutable
    /// state between tests.
    metrics* source{nullptr};
    /// @brief Scenario-owned calls used to observe behavior and cleanup without sharing mutable
    /// state between tests.
    int calls{0};
    /// @brief Scenario-owned fail used to observe behavior and cleanup without sharing mutable
    /// state between tests.
    bool fail{false};
    /// @brief Scenario-owned standard failure used to observe behavior and cleanup without sharing
    /// mutable state between tests.
    bool standard_failure{false};
};
} // namespace

/// @brief Verify reporting is explicit cumulative and reentrant for collection.
TEST(metrics_reporting_tests, reporting_is_explicit_cumulative_and_reentrant_for_collection)
{
    metrics collection;
    recording_output output;
    output.source = &collection;
    EXPECT_EQ(output.calls, 0);
    ASSERT_TRUE(collection.increment("menu.visits"));
    EXPECT_EQ(output.calls, 0);
    ASSERT_TRUE(collection.report(output));
    ASSERT_EQ(output.saved.points.size(), 1U);
    EXPECT_EQ(output.saved.points.front().name, "menu.visits");
    EXPECT_EQ(collection.statistics().series, 2U);
    ASSERT_TRUE(collection.report(output));
    EXPECT_EQ(output.saved.points.size(), 2U);
    EXPECT_EQ(output.calls, 2);
    EXPECT_EQ(collection.statistics().exported, 2U);
}

/// @brief Verify failed export is observable and does not consume metrics.
TEST(metrics_reporting_tests, failed_export_is_observable_and_does_not_consume_metrics)
{
    metrics collection;
    recording_output output;
    ASSERT_TRUE(collection.increment("count", 3));
    output.fail = true;
    EXPECT_FALSE(collection.report(output));
    EXPECT_EQ(output.calls, 1);
    EXPECT_EQ(collection.statistics().export_errors, 1U);
    EXPECT_EQ(collection.statistics().exported, 0U);
    EXPECT_DOUBLE_EQ(collection.snapshot().points.front().value, 3);
    output.standard_failure = true;
    EXPECT_FALSE(collection.report(output));
    EXPECT_EQ(collection.statistics().export_errors, 2U);
    output.fail = false;
    EXPECT_TRUE(collection.report(output));
    EXPECT_DOUBLE_EQ(output.saved.points.front().value, 3);
}

/// @brief Verify disabled collection never invokes output.
TEST(metrics_reporting_tests, disabled_collection_never_invokes_output)
{
    metrics_options options;
    options.enabled = false;
    metrics collection{options};
    recording_output output;
    EXPECT_FALSE(collection.report(output));
    EXPECT_EQ(output.calls, 0);
    EXPECT_EQ(collection.statistics().exported, 0U);
    EXPECT_EQ(collection.statistics().export_errors, 0U);
}
} // namespace atlas::diagnostics::tests
