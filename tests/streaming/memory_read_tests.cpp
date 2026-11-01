/**
 * @file
 * @brief Behavioral coverage for memory read with isolated scenario ownership and cleanup.
 */

#include <atlas/streaming/asset_streamer.hpp>
#include <gtest/gtest.h>

#include <atomic>
#include <thread>

namespace atlas::streaming::tests
{
/// @brief Verify publishes an owned range on a job worker.
TEST(memory_read_tests, publishes_an_owned_range_on_a_job_worker)
{
    jobs::job_system jobs{2};
    auto source = std::make_shared<const asset_bytes>(asset_bytes{10, 20, 30, 40});
    asset_streamer streamer{jobs, {{"menu", memory_asset{source}}}};
    const auto caller = std::this_thread::get_id();
    std::shared_ptr<const asset_bytes> output;
    auto completion = streamer.read(
        {"menu", 1, 2},
        [&](const read_result& result)
        {
            EXPECT_NE(std::this_thread::get_id(), caller);
            ASSERT_TRUE(std::holds_alternative<std::shared_ptr<const asset_bytes>>(result));
            output = std::get<std::shared_ptr<const asset_bytes>>(result);
        });
    completion.get();
    streamer.shutdown();
    source.reset();
    ASSERT_NE(output, nullptr);
    EXPECT_EQ(*output, (asset_bytes{20, 30}));
}

/// @brief Scenario state for invalid range, isolated per test and released through RAII.
struct invalid_range
{
    /// @brief Scenario-owned offset used to observe behavior and cleanup without sharing mutable
    /// state between tests.
    std::uint64_t offset;
    /// @brief Scenario-owned size used to observe behavior and cleanup without sharing mutable
    /// state between tests.
    std::size_t size;
};

/// @brief Scenario state for invalid memory ranges, isolated per test and released through RAII.
class invalid_memory_ranges : public testing::TestWithParam<invalid_range>
{
};

/// @brief Verify returns an error without partial bytes.
TEST_P(invalid_memory_ranges, returns_an_error_without_partial_bytes)
{
    jobs::job_system jobs{1};
    asset_streamer streamer{
        jobs, {{"menu", memory_asset{std::make_shared<const asset_bytes>(asset_bytes{1, 2})}}}};
    const auto range = GetParam();
    streamer
        .read({"menu", range.offset, range.size},
              [](const read_result& result)
              {
                  ASSERT_TRUE(std::holds_alternative<std::exception_ptr>(result));
                  EXPECT_THROW(std::rethrow_exception(std::get<std::exception_ptr>(result)),
                               std::out_of_range);
              })
        .get();
}

INSTANTIATE_TEST_SUITE_P(boundaries, invalid_memory_ranges,
                         testing::Values(invalid_range{3, 0}, invalid_range{1, 2},
                                         invalid_range{UINT64_MAX, 1}));

/// @brief Verify allows an empty range at the end.
TEST(memory_read_tests, allows_an_empty_range_at_the_end)
{
    jobs::job_system jobs{1};
    asset_streamer streamer{jobs, {{"empty", memory_asset{std::make_shared<const asset_bytes>()}}}};
    streamer
        .read({"empty", 0, 0},
              [](const read_result& result)
              {
                  ASSERT_TRUE(std::holds_alternative<std::shared_ptr<const asset_bytes>>(result));
                  EXPECT_TRUE(std::get<std::shared_ptr<const asset_bytes>>(result)->empty());
              })
        .get();
}
} // namespace atlas::streaming::tests
