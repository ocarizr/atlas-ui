/** @file
 * @brief Retained-buffer budgets and replaceable allocator lifetime contracts.
 */
#include <atlas/streaming/asset_streamer.hpp>
#include <gtest/gtest.h>

#include <atomic>
#include <memory_resource>

namespace atlas::streaming::tests
{
/// @brief Thread-safe test resource tracking payload allocation and release.
class tracked_resource final : public std::pmr::memory_resource
{
  public:
    /// @brief Number of payload allocations served by this resource.
    std::atomic<std::size_t> allocations{0};
    /// @brief Number of matching payload deallocations.
    std::atomic<std::size_t> deallocations{0};

  private:
    /// @brief Allocate through the default heap and record the call.
    void* do_allocate(std::size_t bytes, std::size_t alignment) override
    {
        ++allocations;
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }

    /// @brief Return storage to the same upstream with its original size/alignment.
    void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override
    {
        ++deallocations;
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    }

    /// @brief Resources compare by identity.
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }
};

/// @brief A retained result continues consuming admission bytes after callback completion.
TEST(buffer_budget_tests, retained_bytes_are_bounded_until_the_last_consumer_releases_them)
{
    jobs::job_system jobs{2};
    streamer_limits limits;
    limits.max_resident_bytes = 4;
    auto input = std::make_shared<const asset_bytes>(asset_bytes{1, 2, 3, 4});
    asset_streamer streamer{jobs, {{"data", memory_asset{input}}}, limits};
    std::shared_ptr<const asset_bytes> held;
    streamer
        .read({"data", 0, 4},
              [&](const read_result& result)
              {
                  held = std::get<std::shared_ptr<const asset_bytes>>(result);
              })
        .get();
    EXPECT_EQ(streamer.statistics().resident_bytes, 4);
    std::atomic<bool> rejected{false};
    EXPECT_THROW(streamer.read({"data", 0, 1},
                               [&](const read_result&)
                               {
                                   rejected = true;
                               }),
                 std::overflow_error);
    EXPECT_FALSE(rejected.load());
    held.reset();
    EXPECT_EQ(streamer.statistics().resident_bytes, 0);
    streamer
        .read({"data", 0, 4},
              [](const read_result&)
              {
              })
        .get();
    EXPECT_EQ(streamer.statistics().resident_bytes, 0);
    EXPECT_EQ(streamer.statistics().peak_resident_bytes, 4);
}

/// @brief Expired weak observers cannot delay quota reclamation after byte storage is freed.
TEST(buffer_budget_tests, expired_weak_results_do_not_keep_the_byte_reservation)
{
    jobs::job_system jobs{1};
    streamer_limits limits;
    limits.max_resident_bytes = 2;
    auto input = std::make_shared<const asset_bytes>(asset_bytes{1, 2});
    asset_streamer streamer{jobs, {{"data", memory_asset{input}}}, limits};
    std::shared_ptr<const asset_bytes> held;
    std::weak_ptr<const asset_bytes> observer;
    streamer
        .read({"data", 0, 2},
              [&](const read_result& result)
              {
                  held = std::get<std::shared_ptr<const asset_bytes>>(result);
                  observer = held;
              })
        .get();
    held.reset();
    EXPECT_TRUE(observer.expired());
    EXPECT_EQ(streamer.statistics().resident_bytes, 0);
    EXPECT_NO_THROW(streamer
                        .read({"data", 0, 2},
                              [](const read_result&)
                              {
                              })
                        .get());
}

/// @brief Allocator ownership follows retained buffers beyond streamer destruction.
TEST(buffer_budget_tests, injected_resource_outlives_streamer_and_releases_exactly_once)
{
    jobs::job_system jobs{2};
    auto resource = std::make_shared<tracked_resource>();
    std::weak_ptr<tracked_resource> lifetime = resource;
    std::shared_ptr<const asset_bytes> held;
    auto input = std::make_shared<const asset_bytes>(asset_bytes{5, 6});
    {
        asset_streamer streamer{jobs, {{"data", memory_asset{input}}}, {}, resource};
        streamer
            .read({"data", 0, 2},
                  [&](const read_result& result)
                  {
                      held = std::get<std::shared_ptr<const asset_bytes>>(result);
                  })
            .get();
        EXPECT_GE(resource->allocations.load(), 1);
        EXPECT_EQ(resource->deallocations.load(), 0);
    }
    jobs.wait_idle();
    resource.reset();
    ASSERT_FALSE(lifetime.expired());
    EXPECT_EQ(*held, (asset_bytes{5, 6}));
    {
        const auto observer = lifetime.lock();
        held.reset();
        EXPECT_EQ(observer->allocations.load(), observer->deallocations.load());
    }
    EXPECT_TRUE(lifetime.expired());
}

/// @brief Failed reads release their byte reservation before their future becomes ready.
TEST(buffer_budget_tests, failed_read_and_callback_do_not_leak_budget)
{
    jobs::job_system jobs{1};
    streamer_limits limits;
    limits.max_resident_bytes = 1;
    asset_streamer streamer{jobs, {}, limits};
    auto failed = streamer.read({"unknown", 0, 1},
                                [](const read_result&)
                                {
                                    throw std::runtime_error{"callback failed"};
                                });
    EXPECT_THROW(failed.get(), std::runtime_error);
    EXPECT_EQ(streamer.statistics().resident_bytes, 0);
    EXPECT_EQ(streamer.statistics().pending, 0);
    EXPECT_EQ(streamer.statistics().failed, 1);
}
} // namespace atlas::streaming::tests
