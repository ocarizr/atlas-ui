#include <atlas/streaming/AssetStreamer.hpp>
#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <thread>

namespace atlas::streaming::tests
{
TEST(StreamerConcurrencyTests, ServesConcurrentProducersExactlyOnce)
{
    jobs::JobSystem jobs{2};
    std::array<std::atomic<int>, 64> counts{};
    AssetStreamer streamer{jobs, {}, {4, counts.size()}};
    {
        std::vector<std::jthread> producers;
        producers.reserve(4);
        for (std::size_t producer = 0; producer < 4; ++producer)
        {
            producers.emplace_back(
                [&, producer]
                {
                    for (std::size_t index = producer * 16; index < (producer + 1) * 16; ++index)
                    {
                        streamer.read({"unknown", 0, 0},
                                      [&, index](const ReadResult& result)
                                      {
                                          EXPECT_TRUE(
                                              std::holds_alternative<std::exception_ptr>(result));
                                          ++counts[index];
                                      });
                    }
                });
        }
    }
    streamer.shutdown();
    for (const auto& count : counts)
    {
        EXPECT_EQ(count.load(), 1);
    }
}

TEST(StreamerConcurrencyTests, ACompletionCanRequestTheNextChunkWithoutWaiting)
{
    jobs::JobSystem jobs{1};
    std::atomic<int> completed{0};
    AssetStreamer streamer{jobs, {}};
    streamer
        .read({"unknown", 0, 0},
              [&](const ReadResult&)
              {
                  ++completed;
                  streamer.read({"unknown", 0, 0}, [&](const ReadResult&) { ++completed; });
              })
        .get();
    streamer.shutdown();
    EXPECT_EQ(completed.load(), 2);
}

TEST(StreamerConcurrencyTests, DestructionDrainsOutstandingCallbacks)
{
    jobs::JobSystem jobs{2};
    std::atomic<int> completed{0};
    {
        AssetStreamer streamer{jobs, {}};
        for (int index = 0; index < 16; ++index)
        {
            streamer.read({"unknown", 0, 0}, [&](const ReadResult&) { ++completed; });
        }
    }
    EXPECT_EQ(completed.load(), 16);
}
} // namespace atlas::streaming::tests
