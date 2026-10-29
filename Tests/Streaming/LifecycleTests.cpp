#include <atlas/streaming/AssetStreamer.hpp>
#include <gtest/gtest.h>

#include <atomic>
#include <mutex>

namespace atlas::streaming::tests
{
TEST(StreamerLifecycleTests, RejectsInvalidLimits)
{
    jobs::JobSystem jobs{1};
    EXPECT_THROW((AssetStreamer{jobs, {}, {0, 1}}), std::invalid_argument);
    EXPECT_THROW((AssetStreamer{jobs, {}, {1, 0}}), std::invalid_argument);
}

TEST(StreamerLifecycleTests, RejectsInvalidRequestsWithoutCallbacks)
{
    jobs::JobSystem jobs{1};
    AssetStreamer streamer{jobs, {}, {4, 2}};
    std::atomic<bool> called{false};
    EXPECT_THROW(streamer.read({"missing", 0, 1}, {}), std::invalid_argument);
    EXPECT_THROW(streamer.read({"missing", 0, 5}, [&](const ReadResult&) { called = true; }),
                 std::length_error);
    streamer.shutdown();
    EXPECT_FALSE(called.load());
    EXPECT_THROW(streamer.read({"missing", 0, 1}, [](const ReadResult&) {}), std::logic_error);
    EXPECT_NO_THROW(streamer.shutdown());
}

TEST(StreamerLifecycleTests, ReportsUnknownAndUnbackedMemoryAssets)
{
    jobs::JobSystem jobs{1};
    AssetStreamer streamer{jobs, {{"unbacked", MemoryAsset{nullptr}}}};
    streamer
        .read({"unknown", 0, 0},
              [](const ReadResult& result)
              {
                  ASSERT_TRUE(std::holds_alternative<std::exception_ptr>(result));
                  EXPECT_THROW(std::rethrow_exception(std::get<std::exception_ptr>(result)),
                               std::out_of_range);
              })
        .get();
    streamer
        .read({"unbacked", 0, 0},
              [](const ReadResult& result)
              {
                  ASSERT_TRUE(std::holds_alternative<std::exception_ptr>(result));
                  EXPECT_THROW(std::rethrow_exception(std::get<std::exception_ptr>(result)),
                               std::invalid_argument);
              })
        .get();
}

TEST(StreamerLifecycleTests, PropagatesCallbackExceptionsAndDrainsOutstandingWork)
{
    jobs::JobSystem jobs{1};
    AssetStreamer streamer{jobs, {}};
    auto completion = streamer.read({"unknown", 0, 0}, [](const ReadResult&)
                                    { throw std::runtime_error{"consumer failed"}; });
    EXPECT_THROW(completion.get(), std::runtime_error);
    EXPECT_NO_THROW(streamer.shutdown());
}

TEST(StreamerLifecycleTests, RejectsShutdownInsideACompletionJob)
{
    jobs::JobSystem jobs{1};
    AssetStreamer streamer{jobs, {}};
    auto completion =
        streamer.read({"unknown", 0, 0}, [&](const ReadResult&) { streamer.shutdown(); });
    EXPECT_THROW(completion.get(), std::logic_error);
}

TEST(StreamerLifecycleTests, ReportsSchedulerShutdownWithoutCallingTheConsumerOnTheWrongThread)
{
    jobs::JobSystem jobs{1};
    AssetStreamer streamer{jobs, {}};
    jobs.shutdown();
    bool called = false;
    auto completion = streamer.read({"unknown", 0, 0}, [&](const ReadResult&) { called = true; });
    EXPECT_THROW(completion.get(), std::logic_error);
    EXPECT_FALSE(called);
}

TEST(StreamerLifecycleTests, RejectsExcessRequestsWithoutCallingTheirConsumers)
{
    // Open the gate before streamer destruction even if an assertion aborts.
    jobs::JobSystem jobs{2};
    AssetStreamer streamer{jobs, {}, {4, 1}};
    struct ReleaseGate
    {
        std::promise<void> promise;
        std::shared_future<void> ready{promise.get_future().share()};
        std::once_flag opened;
        void open()
        {
            std::call_once(opened, [this] { promise.set_value(); });
        }
        ~ReleaseGate()
        {
            try
            {
                open();
            }
            catch (...)
            {
                std::terminate();
            }
        }
    } release;
    auto accepted = streamer.read({"unknown", 0, 0},
                                  [ready = release.ready](const ReadResult&) { ready.wait(); });
    std::atomic<bool> rejected_callback{false};
    EXPECT_THROW(
        streamer.read({"unknown", 0, 0}, [&](const ReadResult&) { rejected_callback = true; }),
        std::overflow_error);
    release.open();
    accepted.get();
    streamer.shutdown();
    EXPECT_FALSE(rejected_callback.load());
}
} // namespace atlas::streaming::tests
