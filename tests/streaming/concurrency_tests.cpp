/**
 * @file
 * @brief Behavioral coverage for concurrency with isolated scenario ownership and cleanup.
 */

#include <atlas/streaming/asset_streamer.hpp>
#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <thread>

namespace atlas::streaming::tests
{
/// @brief Verify serves concurrent producers exactly once.
TEST(streamer_concurrency_tests, serves_concurrent_producers_exactly_once)
{
    jobs::job_system jobs{2};
    std::array<std::atomic<int>, 64> counts{};
    asset_streamer streamer{jobs, {}, {4, counts.size()}};
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
                                      [&, index](const read_result& result)
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

/// @brief Verify a completion can request the next chunk without waiting.
TEST(streamer_concurrency_tests, a_completion_can_request_the_next_chunk_without_waiting)
{
    jobs::job_system jobs{1};
    std::atomic<int> completed{0};
    asset_streamer streamer{jobs, {}};
    streamer
        .read({"unknown", 0, 0},
              [&](const read_result&)
              {
                  ++completed;
                  streamer.read({"unknown", 0, 0},
                                [&](const read_result&)
                                {
                                    ++completed;
                                });
              })
        .get();
    streamer.shutdown();
    EXPECT_EQ(completed.load(), 2);
}

/// @brief Verify destruction drains outstanding callbacks.
TEST(streamer_concurrency_tests, destruction_drains_outstanding_callbacks)
{
    jobs::job_system jobs{2};
    std::atomic<int> completed{0};
    {
        asset_streamer streamer{jobs, {}};
        for (int index = 0; index < 16; ++index)
        {
            streamer.read({"unknown", 0, 0},
                          [&](const read_result&)
                          {
                              ++completed;
                          });
        }
    }
    EXPECT_EQ(completed.load(), 16);
}
} // namespace atlas::streaming::tests
