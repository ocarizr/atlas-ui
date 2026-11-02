/** @file @brief Streaming diagnostics retain optional destinations without altering read outcomes.
 */
#include <atlas/streaming/asset_streamer.hpp>
#include <gtest/gtest.h>
#include <sstream>

namespace atlas::streaming::tests
{
/// @brief Separate isolated success/failure reads share the same capture and cleanup assertions.
class streaming_diagnostics_test : public ::testing::TestWithParam<bool>
{
};

/// @brief Successful copies and failed lookups retain their original outcome with capture enabled.
TEST_P(streaming_diagnostics_test, captures_outcome_and_drains_before_export)
{
    auto capture = std::make_shared<diagnostics::profiler>();
    jobs::job_system_options options;
    options.worker_count = 2;
    jobs::job_system pool{options};
    auto bytes = std::make_shared<asset_bytes>(4, std::uint8_t{42});
    asset_streamer streamer{pool, {{"data", memory_asset{bytes}}}, {}, {}, {{}, capture}};
    bool failed = false;
    streamer
        .read({GetParam() ? "missing" : "data", 0, 1},
              [&failed](const read_result& result)
              {
                  failed = std::holds_alternative<std::exception_ptr>(result);
              })
        .get();
    streamer.shutdown();
    pool.shutdown();
    EXPECT_EQ(failed, GetParam());
    EXPECT_EQ(streamer.statistics().failed, static_cast<std::size_t>(GetParam()));
    EXPECT_EQ(streamer.statistics().completed, 1U);
    EXPECT_EQ(streamer.statistics().resident_bytes, 0U);
    std::ostringstream trace;
    capture->write_chrome_trace(trace);
    EXPECT_EQ(trace.str().find("read.lifetime") != std::string::npos,
              diagnostics::instrumentation_enabled);
    EXPECT_EQ(trace.str().find("failed_or_cancelled") != std::string::npos,
              diagnostics::instrumentation_enabled && GetParam());
    EXPECT_EQ(trace.str().find("read.copy_chunk") != std::string::npos,
              diagnostics::instrumentation_enabled && !GetParam());
}

INSTANTIATE_TEST_SUITE_P(outcomes, streaming_diagnostics_test, ::testing::Bool());
} // namespace atlas::streaming::tests
