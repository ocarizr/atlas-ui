#include <atlas/streaming/AssetStreamer.hpp>
#include <gtest/gtest.h>

#include <atomic>
#include <thread>

namespace atlas::streaming::tests
{
TEST(MemoryReadTests, PublishesAnOwnedRangeOnAJobWorker)
{
    jobs::JobSystem jobs{2};
    auto source = std::make_shared<const AssetBytes>(AssetBytes{10, 20, 30, 40});
    AssetStreamer streamer{jobs, {{"menu", MemoryAsset{source}}}};
    const auto caller = std::this_thread::get_id();
    std::shared_ptr<const AssetBytes> output;
    auto completion = streamer.read(
        {"menu", 1, 2},
        [&](const ReadResult& result)
        {
            EXPECT_NE(std::this_thread::get_id(), caller);
            ASSERT_TRUE(std::holds_alternative<std::shared_ptr<const AssetBytes>>(result));
            output = std::get<std::shared_ptr<const AssetBytes>>(result);
        });
    completion.get();
    streamer.shutdown();
    source.reset();
    ASSERT_NE(output, nullptr);
    EXPECT_EQ(*output, (AssetBytes{20, 30}));
}

struct InvalidRange
{
    std::uint64_t offset;
    std::size_t size;
};
class InvalidMemoryRanges : public testing::TestWithParam<InvalidRange>
{
};
TEST_P(InvalidMemoryRanges, ReturnsAnErrorWithoutPartialBytes)
{
    jobs::JobSystem jobs{1};
    AssetStreamer streamer{
        jobs, {{"menu", MemoryAsset{std::make_shared<const AssetBytes>(AssetBytes{1, 2})}}}};
    const auto range = GetParam();
    streamer
        .read({"menu", range.offset, range.size},
              [](const ReadResult& result)
              {
                  ASSERT_TRUE(std::holds_alternative<std::exception_ptr>(result));
                  EXPECT_THROW(std::rethrow_exception(std::get<std::exception_ptr>(result)),
                               std::out_of_range);
              })
        .get();
}
INSTANTIATE_TEST_SUITE_P(Boundaries, InvalidMemoryRanges,
                         testing::Values(InvalidRange{3, 0}, InvalidRange{1, 2},
                                         InvalidRange{UINT64_MAX, 1}));

TEST(MemoryReadTests, AllowsAnEmptyRangeAtTheEnd)
{
    jobs::JobSystem jobs{1};
    AssetStreamer streamer{jobs, {{"empty", MemoryAsset{std::make_shared<const AssetBytes>()}}}};
    streamer
        .read({"empty", 0, 0},
              [](const ReadResult& result)
              {
                  ASSERT_TRUE(std::holds_alternative<std::shared_ptr<const AssetBytes>>(result));
                  EXPECT_TRUE(std::get<std::shared_ptr<const AssetBytes>>(result)->empty());
              })
        .get();
}
} // namespace atlas::streaming::tests
