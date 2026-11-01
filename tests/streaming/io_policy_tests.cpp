/** @file
 * @brief Public file-I/O admission limits and drain accounting across selected backends.
 */
#include "temporary_assets.hpp"
#include <atlas/streaming/asset_streamer.hpp>
#include <atomic>
#include <gtest/gtest.h>

namespace atlas::streaming::tests
{
/// @brief Reject a zero disk-concurrency limit before opening storage resources.
TEST(io_policy_tests, rejects_zero_io_concurrency)
{
    jobs::job_system jobs{1};
    streamer_limits limits;
    limits.max_io_requests = 0;
    EXPECT_THROW((asset_streamer{jobs, {}, limits}), std::invalid_argument);
}

/// @brief Accepted file reads drain exactly once without exceeding the configured I/O cap.
TEST(io_policy_tests, bounds_file_operations_and_drains_before_shutdown_returns)
{
    temporary_assets assets;
    assets.write();
    jobs::job_system jobs{2};
    streamer_limits limits;
    limits.max_io_requests = 2;
    asset_streamer streamer{jobs, {{"data", loose_file_asset{assets.path()}}}, limits};
    std::atomic<int> completed{0};
    for (int index = 0; index < 16; ++index)
    {
        streamer.read({"data", 1, 2},
                      [&](const read_result& result)
                      {
                          ASSERT_TRUE(
                              std::holds_alternative<std::shared_ptr<const asset_bytes>>(result));
                          EXPECT_EQ(*std::get<std::shared_ptr<const asset_bytes>>(result),
                                    (asset_bytes{'B', 'C'}));
                          ++completed;
                      });
    }

    streamer.shutdown();
    EXPECT_EQ(completed.load(), 16);
    const auto statistics = streamer.statistics();
    EXPECT_EQ(statistics.io_in_flight, 0);
    EXPECT_GT(statistics.peak_io_in_flight, 0);
    EXPECT_LE(statistics.peak_io_in_flight, 2);
    EXPECT_TRUE(std::filesystem::remove(assets.path()));
}
} // namespace atlas::streaming::tests
