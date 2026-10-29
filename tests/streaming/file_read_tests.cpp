#include <atlas/streaming/asset_streamer.hpp>
#include <gtest/gtest.h>

#include "temporary_assets.hpp"

#include <atomic>

namespace atlas::streaming::tests
{
class file_read_tests : public testing::Test
{
  protected:
    temporary_assets assets;
    jobs::job_system jobs{2};
};

TEST_F(file_read_tests, loads_only_the_requested_range_from_a_loose_asset)
{
    assets.write();
    asset_streamer streamer{jobs, {{"menu", loose_file_asset{assets.path()}}}};
    streamer
        .read({"menu", 1, 2},
              [](const read_result& result)
              {
                  ASSERT_TRUE(std::holds_alternative<std::shared_ptr<const asset_bytes>>(result));
                  EXPECT_EQ(*std::get<std::shared_ptr<const asset_bytes>>(result),
                            (asset_bytes{'B', 'C'}));
              })
        .get();
    streamer.shutdown();
    EXPECT_TRUE(std::filesystem::remove(assets.path()));
}

TEST_F(file_read_tests, reports_missing_files_and_keeps_serving_requests)
{
    asset_streamer streamer{jobs, {{"menu", loose_file_asset{assets.path()}}}};
    streamer
        .read({"menu", 0, 1},
              [](const read_result& result)
              {
                  ASSERT_TRUE(std::holds_alternative<std::exception_ptr>(result));
                  EXPECT_THROW(std::rethrow_exception(std::get<std::exception_ptr>(result)),
                               std::runtime_error);
              })
        .get();
    assets.write();
    streamer
        .read({"menu", 0, 1},
              [](const read_result& result)
              {
                  ASSERT_TRUE(std::holds_alternative<std::shared_ptr<const asset_bytes>>(result));
                  EXPECT_EQ(*std::get<std::shared_ptr<const asset_bytes>>(result),
                            (asset_bytes{'A'}));
              })
        .get();
}

TEST_F(file_read_tests, rejects_ranges_beyond_the_file_without_returning_partial_data)
{
    assets.write();
    asset_streamer streamer{jobs, {{"menu", loose_file_asset{assets.path()}}}};
    streamer
        .read({"menu", 3, 2},
              [](const read_result& result)
              {
                  ASSERT_TRUE(std::holds_alternative<std::exception_ptr>(result));
                  EXPECT_THROW(std::rethrow_exception(std::get<std::exception_ptr>(result)),
                               std::out_of_range);
              })
        .get();
}

TEST_F(file_read_tests, drains_accepted_file_reads_during_shutdown)
{
    assets.write();
    std::atomic<int> completed{0};
    asset_streamer streamer{jobs, {{"menu", loose_file_asset{assets.path()}}}};
    for (int index = 0; index < 16; ++index)
    {
        streamer.read({"menu", 0, 4},
                      [&](const read_result& result)
                      {
                          EXPECT_TRUE(
                              std::holds_alternative<std::shared_ptr<const asset_bytes>>(result));
                          ++completed;
                      });
    }
    streamer.shutdown();
    EXPECT_EQ(completed.load(), 16);
}
} // namespace atlas::streaming::tests
