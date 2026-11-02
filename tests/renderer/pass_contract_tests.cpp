/** @file @brief Immutable pass snapshots and portable pipeline validation. */
#include <atlas/renderer/passes.hpp>
#include <gtest/gtest.h>

namespace atlas::renderer::tests
{
/// @brief Caller mutations cannot change an already published frame snapshot.
TEST(pass_contract, snapshot_copies_nested_packets)
{
    std::vector<pass_description> passes(1);
    passes[0].draws.resize(1);
    frame_plan snapshot{passes};
    passes[0].draws[0].vertex_count = 99;
    EXPECT_EQ(snapshot.passes()[0].draws[0].vertex_count, 0U);
}

/// @brief Empty frames are rejected before backend interaction.
TEST(pass_contract, rejects_empty_frame)
{
    EXPECT_THROW((frame_plan{std::span<const pass_description>{}}), std::invalid_argument);
}

/// @brief Custom shader entry points must be explicit identifiers.
TEST(pass_contract, rejects_invalid_shader_entry)
{
    pipeline_description description;
    description.program = shader_program::custom;
    description.source = "not compiled by portable validation";
    description.vertex_entry = "invalid entry";
    EXPECT_THROW(description.validate(), std::invalid_argument);
}

/// @brief Resource and command quotas remain explicit rather than silently unbounded.
TEST(pass_contract, rejects_empty_capacities_and_unsupported_shader_inputs)
{
    pass_limits limits;
    limits.max_frames = 0;
    EXPECT_THROW(limits.validate(), std::invalid_argument);
    pipeline_description description;
    description.program = shader_program::custom;
    EXPECT_THROW(description.validate(), std::invalid_argument);
    description.source.resize(1024U * 1024 + 1, ' ');
    EXPECT_THROW(description.validate(), std::invalid_argument);
}
} // namespace atlas::renderer::tests
