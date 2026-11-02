/** @file @brief Bounded repeated allocation/upload/retirement returns logical occupancy to zero. */
#include "resource_test_support.hpp"

namespace atlas::renderer::tests
{
/// @brief Small admission quotas stay reusable instead of accumulating retired resources/staging.
TEST_F(resource_test, repeated_pressure_reclaims_resources_descriptors_and_uploads)
{
    const std::array<std::byte, 16> rgba{};
    for (std::size_t round = 0; round < 24; ++round)
    {
        completion_token done{};
        for (std::size_t index = 0; index < 2; ++index)
        {
            const auto texture = resources.create_texture({2, 2});
            const auto view = resources.create_view(texture);
            done = resources.upload(texture, rgba);
            resources.release(view);
            resources.release(texture);
        }

        resources.wait(done);
        resources.collect();
        const auto state = resources.statistics();
        EXPECT_EQ(state.live_resources, 0U);
        EXPECT_EQ(state.retired_resources, 0U);
        EXPECT_EQ(state.live_descriptors, 0U);
        EXPECT_EQ(state.retired_descriptors, 0U);
        EXPECT_EQ(state.staging_bytes, 0U);
        EXPECT_EQ(state.allocation_bytes, 0U);
    }
}
} // namespace atlas::renderer::tests
