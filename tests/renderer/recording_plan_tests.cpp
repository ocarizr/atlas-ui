/** @file @brief Stable coarse recording partitions independent of graphics SDKs. */
#include "recording_plan.hpp"
#include <array>
#include <gtest/gtest.h>

namespace atlas::renderer::tests
{
/// @brief Small workloads keep the sequential path despite available workers.
TEST(recording_plan, small_frame_uses_one_batch)
{
    const std::array<std::size_t, 2> draws{1, 2};
    EXPECT_EQ(detail::plan_recording(draws, {1024, 4}).size(), 1U);
}

/// @brief One large pass can be partitioned without repeating its load or store operations.
TEST(recording_plan, partitions_single_pass_in_stable_order)
{
    const std::array<std::size_t, 1> draws{12};
    const auto batches = detail::plan_recording(draws, {3, 4});
    ASSERT_EQ(batches.size(), 4U);
    for (std::size_t index = 0; index < batches.size(); ++index)
    {
        ASSERT_EQ(batches[index].size(), 1U);
        const auto& section = batches[index][0];
        EXPECT_EQ(section.pass, 0U);
        EXPECT_EQ(section.begin, index * 3);
        EXPECT_EQ(section.count, 3U);
        EXPECT_EQ(section.first, index == 0);
        EXPECT_EQ(section.last, index == 3);
    }
}

/// @brief Clear-only passes remain in order between draw ranges.
TEST(recording_plan, preserves_clear_only_passes_and_caps_batches)
{
    const std::array<std::size_t, 3> draws{5, 0, 7};
    const auto batches = detail::plan_recording(draws, {2, 2});
    ASSERT_EQ(batches.size(), 2U);
    std::size_t count = 0;
    std::size_t clears = 0;
    for (const auto& batch : batches)
    {
        for (const auto& section : batch)
        {
            count += section.count;
            clears += section.pass == 1 ? 1 : 0;
        }
    }

    EXPECT_EQ(count, 12U);
    EXPECT_EQ(clears, 1U);
}
} // namespace atlas::renderer::tests
