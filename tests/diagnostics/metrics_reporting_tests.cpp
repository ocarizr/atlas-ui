#include <atlas/diagnostics/metrics.hpp>
#include <gtest/gtest.h>

#include <stdexcept>

namespace atlas::diagnostics::tests
{
namespace
{
class recording_output final : public metrics_output
{
  public:
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
    metric_snapshot saved;
    metrics* source{nullptr};
    int calls{0};
    bool fail{false};
    bool standard_failure{false};
};
} // namespace

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
