/** @file @brief Resource configuration rejects invalid requests before native allocation. */
#include <atlas/renderer/resources.hpp>
#include <gtest/gtest.h>

namespace atlas::renderer::tests
{
/// @brief Zero sizes and unsupported texture extents are rejected independently of backend support.
TEST(resource_contract_tests, validates_resource_descriptions)
{
    EXPECT_THROW(buffer_description{}.validate(), std::invalid_argument);
    EXPECT_NO_THROW((buffer_description{1}.validate()));
    EXPECT_THROW(texture_description{}.validate(), std::invalid_argument);
    EXPECT_THROW((texture_description{16385, 1}.validate()), std::invalid_argument);
    EXPECT_NO_THROW((texture_description{7, 3}.validate()));
}

/// @brief All bounded registries and staging capacities must be positive.
TEST(resource_contract_tests, rejects_zero_capacity)
{
    resource_limits limits;
    limits.max_resources = 0;
    EXPECT_THROW(limits.validate(), std::invalid_argument);
}

/// @brief Reject invalid mip chains and incompatible role/format combinations.
TEST(resource_contract_tests, bounds_format_and_mip_chain)
{
    const texture_description atlas{7, 3, texture_role::sampled, texture_format::r8_unorm, 3};
    EXPECT_NO_THROW(atlas.validate());
    EXPECT_EQ(atlas.payload_size(), 25U);
    EXPECT_THROW(
        (texture_description{7, 3, texture_role::sampled, texture_format::r8_unorm, 4}.validate()),
        std::invalid_argument);
    EXPECT_THROW((texture_description{7, 3, texture_role::color_target, texture_format::r8_unorm}
                      .validate()),
                 std::invalid_argument);
}
} // namespace atlas::renderer::tests
