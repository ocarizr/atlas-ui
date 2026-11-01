/** @file
 * @brief Deterministic cancellation after admission, including copy-child failure cleanup.
 */
#include <atlas/streaming/asset_streamer.hpp>
#include <gtest/gtest.h>

namespace atlas::streaming::tests
{
/// @brief Thread-safe resource that signals cancellation at a controlled allocation boundary.
class cancelling_resource final : public std::pmr::memory_resource
{
  public:
    /// @brief Share the caller's stop state without retaining borrowed test variables.
    explicit cancelling_resource(std::stop_source cancellation)
        : cancellation_{std::move(cancellation)}
    {
    }

  private:
    /// @brief Signal cancellation after admission but before byte copying can begin.
    void* do_allocate(std::size_t bytes, std::size_t alignment) override
    {
        cancellation_.request_stop();
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }

    /// @brief Return every admitted allocation with its original size/alignment.
    void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override
    {
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    }

    /// @brief Allocation resources compare by identity.
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }

    /// @brief Owned shared cancellation capability, safe across allocation threads.
    std::stop_source cancellation_;
};

/// @brief The same cancellation/cleanup contract applies to both copy execution paths.
class cancelled_copy_tests : public testing::TestWithParam<bool>
{
};

/// @brief Cancellation after allocation cannot publish partial bytes or strand admission claims.
TEST_P(cancelled_copy_tests, reports_cancellation_and_releases_every_copy_claim)
{
    jobs::job_system jobs{2};
    std::stop_source stop;
    auto input = std::make_shared<const asset_bytes>(256, std::uint8_t{23});
    streamer_limits limits;
    limits.parallel_copy_threshold = GetParam() ? 128 : 0;
    limits.copy_grain_bytes = 64;
    asset_streamer streamer{
        jobs, {{"data", memory_asset{input}}}, limits, std::make_shared<cancelling_resource>(stop)};
    streamer
        .read({"data", 0, input->size()},
              [](const read_result& result)
              {
                  ASSERT_TRUE(std::holds_alternative<std::exception_ptr>(result));
                  EXPECT_THROW(std::rethrow_exception(std::get<std::exception_ptr>(result)),
                               jobs::job_cancelled);
              },
              {jobs::job_priority::background, stop.get_token()})
        .get();
    streamer.shutdown();
    jobs.wait_idle();
    EXPECT_EQ(streamer.statistics().completed, 1);
    EXPECT_EQ(streamer.statistics().failed, 1);
    EXPECT_EQ(streamer.statistics().pending, 0);
    EXPECT_EQ(streamer.statistics().resident_bytes, 0);
    EXPECT_EQ(streamer.statistics().parallel_copies, GetParam() ? 1 : 0);
    EXPECT_EQ(streamer.statistics().serial_copies, GetParam() ? 0 : 1);
}

INSTANTIATE_TEST_SUITE_P(execution_paths, cancelled_copy_tests, testing::Bool());
} // namespace atlas::streaming::tests
