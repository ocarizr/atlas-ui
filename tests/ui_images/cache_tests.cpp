/** @file @brief Ready-job acquisition, cancellation and retained byte accounting. */
#include "image_test_support.hpp"
#include <semaphore>

namespace atlas::ui_images::tests
{
/// @brief Decoding publishes intrinsic dimensions and old frames keep their budget charge.
TEST_F(cache_test, ReadyPixelsRemainChargedUntilTheLastFrameIsReleased)
{
    const auto key = cache.request({"pixel", 0, 4}, {16, 4});
    const auto pending = cache.snapshot();
    EXPECT_EQ(pending.find(key)->status, image_status::pending);
    EXPECT_EQ(intrinsic_size(*pending.find(key)), (ui::layout_extent{32, 32}));
    settle(key);
    auto ready = cache.snapshot();
    ASSERT_EQ(ready.find(key)->status, image_status::decoded);
    EXPECT_EQ(intrinsic_size(*ready.find(key)), (ui::layout_extent{1, 1}));
    cache.release(key);
    EXPECT_EQ(cache.statistics().reserved_bytes, 4);
    ready = {};
    EXPECT_EQ(cache.statistics().reserved_bytes, 0);
}

/// @brief Missing sources preserve the original error while cancellation suppresses decoded data.
TEST_F(cache_test, FailureAndCancellationRetainAnExplicitPlaceholderState)
{
    const auto bad = cache.request({"missing", 0, 4}, {16, 4});
    settle(bad);
    auto failure = cache.snapshot();
    EXPECT_EQ(failure.find(bad)->status, image_status::failed);
    EXPECT_TRUE(failure.find(bad)->error);
    const auto key = cache.request({"pixel", 0, 4}, {16, 4});
    cache.cancel(key);
    settle(key);
    EXPECT_EQ(cache.snapshot().find(key)->status, image_status::cancelled);
    EXPECT_FALSE(cache.snapshot().find(key)->pixels);
}

/// @brief Old retained pixels prevent silent quota reuse; released identities never recur.
TEST_F(cache_test, RetainedFramesEnforceAggregateAdmission)
{
    const auto first = cache.request({"pixel", 0, 4}, {16, 4});
    const auto second = cache.request({"pixel", 0, 4}, {16, 4});
    settle(first);
    settle(second);
    auto old = cache.snapshot();
    cache.release(first);
    EXPECT_THROW(cache.request({"pixel", 0, 4}, {16, 4}), std::overflow_error);
    old = {};
    const auto next = cache.request({"pixel", 0, 4}, {16, 4});
    EXPECT_GT(next, second);
    EXPECT_THROW(cache.cancel(first), std::invalid_argument);
}

class gated_decoder final : public renderer_content::image_decoder
{
  public:
    mutable std::binary_semaphore entered{0}; ///< Decode-entry signal.
    mutable std::binary_semaphore release{0}; ///< Controlled release, with a diagnostic timeout.

    std::shared_ptr<const renderer_content::image_data>
    decode(std::span<const std::uint8_t> bytes, renderer_content::image_limits limits,
           std::shared_ptr<std::pmr::memory_resource> memory, std::stop_token stop) const override
    {
        entered.release();
        if (!release.try_acquire_for(std::chrono::seconds{5}))
        {
            throw std::runtime_error("decode gate timeout");
        }

        return pixel_decoder{}.decode(bytes, limits, std::move(memory), std::move(stop));
    } ///< Only the test deliberately parks a worker to exercise accepted-work ownership.
}; ///< Accepted callbacks retain this codec, including after an early assertion failure.

/// @brief Releasing a pending entry cannot return capacity while accepted decode still owns it.
TEST_F(cache_test, CancellationRetainsReservationUntilAcceptedWorkDrains)
{
    const auto decoder = std::make_shared<gated_decoder>();
    image_cache controlled{streamer, decoder, {2, 4}};
    const auto key = controlled.request({"pixel", 0, 4}, {16, 4});
    ASSERT_TRUE(decoder->entered.try_acquire_for(std::chrono::seconds{5}));
    controlled.release(key);
    EXPECT_EQ(controlled.statistics().reserved_bytes, 4);
    EXPECT_THROW(controlled.request({"pixel", 0, 4}, {16, 4}), std::overflow_error);
    decoder->release.release();
    workers.wait_idle();
    EXPECT_EQ(controlled.statistics().reserved_bytes, 0);
}

/// @brief Request/owner destruction leaves accepted callback resources alive until completion.
TEST_F(cache_test, DestructionCancelsWithoutBorrowingTheCacheFacade)
{
    const auto decoder = std::make_shared<gated_decoder>();
    std::weak_ptr<std::pmr::memory_resource> retained;
    {
        auto memory = std::make_shared<std::pmr::synchronized_pool_resource>();
        retained = memory;
        image_cache temporary{streamer, decoder, {1, 4}, memory};
        (void)temporary.request({"pixel", 0, 4}, {16, 4});
        ASSERT_TRUE(decoder->entered.try_acquire_for(std::chrono::seconds{5}));
    }
    EXPECT_FALSE(retained.expired());
    decoder->release.release();
    workers.wait_idle();
    EXPECT_TRUE(retained.expired());
}

/// @brief Configuration and key errors neither admit work nor modify owner-visible entries.
TEST_F(cache_test, InvalidAdmissionLeavesTheCacheUnchanged)
{
    EXPECT_THROW((image_cache{streamer, {}}), std::invalid_argument);
    EXPECT_THROW((image_cache{streamer, std::make_shared<pixel_decoder>(), {0, 4}}),
                 std::invalid_argument);
    EXPECT_THROW(cache.request({"pixel", 0, 4}, {0, 4}), std::invalid_argument);
    EXPECT_THROW(cache.release(0), std::invalid_argument);
    EXPECT_EQ(cache.statistics().images, 0);
    EXPECT_EQ(cache.statistics().reserved_bytes, 0);
    EXPECT_TRUE(image_frame{}.entries().empty());
    EXPECT_EQ(image_frame{}.find(1), nullptr);
    EXPECT_THROW(intrinsic_size({}, {-1, 0}), std::invalid_argument);
}

/// @brief Live-entry quotas are independent of decoded byte quotas and restored on release.
TEST_F(cache_test, EntryQuotaIsExplicitAndReusableAfterRelease)
{
    image_cache bounded{streamer, std::make_shared<pixel_decoder>(), {1, 8}};
    const auto key = bounded.request({"pixel", 0, 4}, {16, 4});
    EXPECT_THROW(bounded.request({"pixel", 0, 4}, {16, 4}), std::overflow_error);
    bounded.release(key);
    const auto next = bounded.request({"pixel", 0, 4}, {16, 4});
    EXPECT_GT(next, key);
}

/// @brief Worker misuse is rejected before mutable owner state is observed.
TEST_F(cache_test, WorkerCannotPollTheCoordinatorCache)
{
    auto work = workers.dispatcher().run({"cache.worker-misuse"}, &cache,
                                         [](jobs::job_context&, image_cache*& target)
                                         {
                                             EXPECT_THROW(target->poll(), std::logic_error);
                                         });
    work.wait();
    EXPECT_EQ(cache.statistics().images, 0);
}

class observing_resource final : public std::pmr::memory_resource
{
  public:
    std::function<void()>
        on_pixel_release; ///< Installed only after jobs drain and entries release.
  private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override
    {
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    } ///< Preserve the system allocation behavior.

    void do_deallocate(void* address, std::size_t bytes, std::size_t alignment) override
    {
        if (bytes == 4 && on_pixel_release)
        {
            on_pixel_release();
        }

        std::pmr::new_delete_resource()->deallocate(address, bytes, alignment);
    } ///< Observe the precise pixel free boundary, before actual deallocation.

    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    } ///< No cross-resource deallocation.
}; ///< Per-test allocation observation, with no production instrumentation hook.

/// @brief Returning budget must follow pixel destruction, including the final alias destructor.
TEST_F(cache_test, LastPixelDeallocationStillOwnsItsBudgetCharge)
{
    auto memory = std::make_shared<observing_resource>();
    image_cache controlled{streamer, std::make_shared<pixel_decoder>(), {1, 4}, memory};
    const auto key = controlled.request({"pixel", 0, 4}, {16, 4});
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (std::chrono::steady_clock::now() < deadline)
    {
        controlled.poll();
        if (controlled.snapshot().find(key)->status != image_status::pending)
        {
            break;
        }

        std::this_thread::yield();
    }

    workers.wait_idle();
    auto frame = controlled.snapshot();
    ASSERT_EQ(frame.find(key)->status, image_status::decoded);
    controlled.release(key);
    std::size_t charge_at_free = 0;
    memory->on_pixel_release = [&]
    {
        charge_at_free = controlled.statistics().reserved_bytes;
    };
    frame = {};
    memory->on_pixel_release = {};
    EXPECT_EQ(charge_at_free, 4);
    EXPECT_EQ(controlled.statistics().reserved_bytes, 0);
}
} // namespace atlas::ui_images::tests
