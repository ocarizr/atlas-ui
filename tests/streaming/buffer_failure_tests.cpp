/** @file
 * @brief Allocation failure, cancellation and quota admission under concurrent producers.
 */
#include <array>
#include <atlas/streaming/asset_streamer.hpp>
#include <atomic>
#include <barrier>
#include <gtest/gtest.h>
#include <thread>

namespace atlas::streaming::tests
{
/// @brief Deterministically failing payload resource; no ambient low-memory assumptions.
class rejecting_resource final : public std::pmr::memory_resource
{
  private:
    /// @brief Reject allocation with the same failure a real allocator would report.
    void* do_allocate(std::size_t, std::size_t) override
    {
        throw std::bad_alloc{};
    }

    /// @brief No storage can be returned because allocation always fails.
    void do_deallocate(void*, std::size_t, std::size_t) override
    {
    }

    /// @brief Compare by resource identity.
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }
};

/// @brief Allocation failure is an asynchronous read error with no partial result or leaked quota.
TEST(buffer_failure_tests, allocator_failure_delivers_error_and_releases_admission)
{
    jobs::job_system jobs{1};
    auto input = std::make_shared<const asset_bytes>(asset_bytes{1, 2});
    asset_streamer streamer{
        jobs, {{"data", memory_asset{input}}}, {}, std::make_shared<rejecting_resource>()};
    streamer
        .read({"data", 0, 2},
              [](const read_result& result)
              {
                  ASSERT_TRUE(std::holds_alternative<std::exception_ptr>(result));
                  EXPECT_THROW(std::rethrow_exception(std::get<std::exception_ptr>(result)),
                               std::bad_alloc);
              })
        .get();
    EXPECT_EQ(streamer.statistics().resident_bytes, 0);
    EXPECT_EQ(streamer.statistics().failed, 1);
}

/// @brief Cancelling an already-queued file request wins over opening a missing source.
TEST(buffer_failure_tests, pre_cancelled_file_does_not_open_storage)
{
    jobs::job_system jobs{1};
    asset_streamer streamer{jobs, {{"data", loose_file_asset{std::filesystem::path{}}}}};
    std::stop_source stop;
    stop.request_stop();
    streamer
        .read({"data", 0, 1},
              [](const read_result& result)
              {
                  ASSERT_TRUE(std::holds_alternative<std::exception_ptr>(result));
                  EXPECT_THROW(std::rethrow_exception(std::get<std::exception_ptr>(result)),
                               jobs::job_cancelled);
              },
              {jobs::job_priority::background, stop.get_token()})
        .get();
    EXPECT_EQ(streamer.statistics().resident_bytes, 0);
}

/// @brief Concurrent read admission cannot exceed the retained-output quota.
TEST(buffer_failure_tests, producers_share_one_atomic_byte_budget)
{
    jobs::job_system jobs{2};
    auto input = std::make_shared<const asset_bytes>(asset_bytes{1, 2, 3, 4});
    streamer_limits limits;
    limits.max_resident_bytes = 4;
    asset_streamer streamer{jobs, {{"data", memory_asset{input}}}, limits};
    std::array<std::shared_ptr<const asset_bytes>, 4> held;
    std::atomic<int> accepted{0};
    std::atomic<int> rejected{0};
    std::atomic<int> unexpected{0};
    std::barrier ready{5};
    {
        std::vector<std::jthread> producers;
        producers.reserve(held.size());
        for (std::size_t index = 0; index < held.size(); ++index)
        {
            producers.emplace_back(
                [&, index]
                {
                    ready.arrive_and_wait();
                    try
                    {
                        streamer
                            .read({"data", 0, 4},
                                  [&, index](const read_result& result)
                                  {
                                      held[index] =
                                          std::get<std::shared_ptr<const asset_bytes>>(result);
                                  })
                            .get();
                        ++accepted;
                    }
                    catch (const std::overflow_error&)
                    {
                        ++rejected;
                    }
                    catch (...)
                    {
                        ++unexpected;
                    }
                });
        }

        ready.arrive_and_wait();
    }
    EXPECT_EQ(accepted.load(), 1);
    EXPECT_EQ(rejected.load(), 3);
    EXPECT_EQ(unexpected.load(), 0);
    EXPECT_EQ(streamer.statistics().peak_resident_bytes, 4);
    held = {};
    EXPECT_EQ(streamer.statistics().resident_bytes, 0);
}
} // namespace atlas::streaming::tests
