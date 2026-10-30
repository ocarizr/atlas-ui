#include <atlas/streaming/asset_streamer.hpp>
#include <gtest/gtest.h>

#include <atomic>
#include <mutex>

namespace atlas::streaming::tests
{
TEST(streamer_lifecycle_tests, rejects_invalid_limits)
{
    jobs::job_system jobs{1};
    EXPECT_THROW((asset_streamer{jobs, {}, {0, 1}}), std::invalid_argument);
    EXPECT_THROW((asset_streamer{jobs, {}, {1, 0}}), std::invalid_argument);
}

TEST(streamer_lifecycle_tests, rejects_invalid_requests_without_callbacks)
{
    jobs::job_system jobs{1};
    asset_streamer streamer{jobs, {}, {4, 2}};
    std::atomic<bool> called{false};
    EXPECT_THROW(streamer.read({"missing", 0, 1}, {}), std::invalid_argument);
    EXPECT_THROW(streamer.read({"missing", 0, 5}, [&](const read_result&) { called = true; }),
                 std::length_error);
    streamer.shutdown();
    EXPECT_FALSE(called.load());
    EXPECT_THROW(streamer.read({"missing", 0, 1}, [](const read_result&) {}), std::logic_error);
    EXPECT_NO_THROW(streamer.shutdown());
}

TEST(streamer_lifecycle_tests, reports_unknown_and_unbacked_memory_assets)
{
    jobs::job_system jobs{1};
    asset_streamer streamer{jobs, {{"unbacked", memory_asset{nullptr}}}};
    streamer
        .read({"unknown", 0, 0},
              [](const read_result& result)
              {
                  ASSERT_TRUE(std::holds_alternative<std::exception_ptr>(result));
                  EXPECT_THROW(std::rethrow_exception(std::get<std::exception_ptr>(result)),
                               std::out_of_range);
              })
        .get();
    streamer
        .read({"unbacked", 0, 0},
              [](const read_result& result)
              {
                  ASSERT_TRUE(std::holds_alternative<std::exception_ptr>(result));
                  EXPECT_THROW(std::rethrow_exception(std::get<std::exception_ptr>(result)),
                               std::invalid_argument);
              })
        .get();
}

TEST(streamer_lifecycle_tests, propagates_callback_exceptions_and_drains_outstanding_work)
{
    jobs::job_system jobs{1};
    asset_streamer streamer{jobs, {}};
    auto completion = streamer.read({"unknown", 0, 0}, [](const read_result&)
                                    { throw std::runtime_error{"consumer failed"}; });
    EXPECT_THROW(completion.get(), std::runtime_error);
    EXPECT_NO_THROW(streamer.shutdown());
}

TEST(streamer_lifecycle_tests, rejects_shutdown_inside_a_completion_job)
{
    jobs::job_system jobs{1};
    asset_streamer streamer{jobs, {}};
    auto completion =
        streamer.read({"unknown", 0, 0}, [&](const read_result&) { streamer.shutdown(); });
    EXPECT_THROW(completion.get(), std::logic_error);
}

TEST(streamer_lifecycle_tests,
     reports_scheduler_shutdown_without_calling_the_consumer_on_the_wrong_thread)
{
    jobs::job_system jobs{1};
    asset_streamer streamer{jobs, {}};
    jobs.shutdown();
    bool called = false;
    auto completion = streamer.read({"unknown", 0, 0}, [&](const read_result&) { called = true; });
    EXPECT_THROW(completion.get(), std::logic_error);
    EXPECT_FALSE(called);
}

TEST(streamer_lifecycle_tests, rejects_excess_requests_without_calling_their_consumers)
{
    // Open the gate before streamer destruction even if an assertion aborts.
    jobs::job_system jobs{2};
    asset_streamer streamer{jobs, {}, {4, 1}};
    struct release_gate
    {
        std::promise<void> promise;
        std::shared_future<void> ready{promise.get_future().share()};
        std::once_flag opened;
        void open()
        {
            std::call_once(opened, [this] { promise.set_value(); });
        }
        ~release_gate()
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
                                  [ready = release.ready](const read_result&) { ready.wait(); });
    std::atomic<bool> rejected_callback{false};
    EXPECT_THROW(
        streamer.read({"unknown", 0, 0}, [&](const read_result&) { rejected_callback = true; }),
        std::overflow_error);
    release.open();
    accepted.get();
    streamer.shutdown();
    EXPECT_FALSE(rejected_callback.load());
}
} // namespace atlas::streaming::tests
