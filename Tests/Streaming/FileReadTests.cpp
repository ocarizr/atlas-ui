#include <atlas/streaming/AssetStreamer.hpp>
#include <gtest/gtest.h>

#include "TemporaryAssets.hpp"

#include <atomic>

namespace atlas::streaming::tests
{
class FileReadTests : public testing::Test
{
  protected:
    TemporaryAssets assets;
    jobs::JobSystem jobs{2};
};

TEST_F(FileReadTests, LoadsOnlyTheRequestedRangeFromALooseAsset)
{
    assets.write();
    AssetStreamer streamer{jobs, {{"menu", LooseFileAsset{assets.path()}}}};
    streamer
        .read({"menu", 1, 2},
              [](const ReadResult& result)
              {
                  ASSERT_TRUE(std::holds_alternative<std::shared_ptr<const AssetBytes>>(result));
                  EXPECT_EQ(*std::get<std::shared_ptr<const AssetBytes>>(result),
                            (AssetBytes{'B', 'C'}));
              })
        .get();
    streamer.shutdown();
    EXPECT_TRUE(std::filesystem::remove(assets.path()));
}

TEST_F(FileReadTests, ReportsMissingFilesAndKeepsServingRequests)
{
    AssetStreamer streamer{jobs, {{"menu", LooseFileAsset{assets.path()}}}};
    streamer
        .read({"menu", 0, 1},
              [](const ReadResult& result)
              {
                  ASSERT_TRUE(std::holds_alternative<std::exception_ptr>(result));
                  EXPECT_THROW(std::rethrow_exception(std::get<std::exception_ptr>(result)),
                               std::runtime_error);
              })
        .get();
    assets.write();
    streamer
        .read({"menu", 0, 1},
              [](const ReadResult& result)
              {
                  ASSERT_TRUE(std::holds_alternative<std::shared_ptr<const AssetBytes>>(result));
                  EXPECT_EQ(*std::get<std::shared_ptr<const AssetBytes>>(result),
                            (AssetBytes{'A'}));
              })
        .get();
}

TEST_F(FileReadTests, RejectsRangesBeyondTheFileWithoutReturningPartialData)
{
    assets.write();
    AssetStreamer streamer{jobs, {{"menu", LooseFileAsset{assets.path()}}}};
    streamer
        .read({"menu", 3, 2},
              [](const ReadResult& result)
              {
                  ASSERT_TRUE(std::holds_alternative<std::exception_ptr>(result));
                  EXPECT_THROW(std::rethrow_exception(std::get<std::exception_ptr>(result)),
                               std::out_of_range);
              })
        .get();
}

TEST_F(FileReadTests, DrainsAcceptedFileReadsDuringShutdown)
{
    assets.write();
    std::atomic<int> completed{0};
    AssetStreamer streamer{jobs, {{"menu", LooseFileAsset{assets.path()}}}};
    for (int index = 0; index < 16; ++index)
    {
        streamer.read({"menu", 0, 4},
                      [&](const ReadResult& result)
                      {
                          EXPECT_TRUE(
                              std::holds_alternative<std::shared_ptr<const AssetBytes>>(result));
                          ++completed;
                      });
    }
    streamer.shutdown();
    EXPECT_EQ(completed.load(), 16);
}
} // namespace atlas::streaming::tests
