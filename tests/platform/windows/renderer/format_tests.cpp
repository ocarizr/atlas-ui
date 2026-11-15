/** @file @brief Reference pixels and fence invariants for typed color and atlas resources. */
#include "pass_test_support.hpp"
#include <algorithm>

namespace atlas::renderer::tests
{
/// @brief The same encoded input decodes differently according to its texture format.
TEST_F(pass_test, srgb_sampling_preserves_linear_alpha)
{
    triangle({1, 1, 1, 1});
    const auto linear = resources.create_texture({1, 1});
    const auto srgb =
        resources.create_texture({1, 1, texture_role::sampled, texture_format::rgba8_srgb});
    const std::array texel{std::byte{128}, std::byte{128}, std::byte{128}, std::byte{128}};
    pipeline_description description;
    description.program = shader_program::textured;
    pipeline = renderer.create_pipeline(description);
    auto draw = pass();
    const auto sample = [&](texture_handle texture)
    {
        resources.upload(texture, texel);
        draw.draws.front().texture = resources.create_view(texture);
        const std::array passes{draw};
        renderer.submit(frame_plan{passes});
        return pixel(4, 4);
    };
    EXPECT_EQ(sample(linear), (std::array<unsigned, 4>{128, 128, 128, 128}));
    const auto decoded = sample(srgb);
    EXPECT_NEAR(decoded[0], 55, 1);
    EXPECT_EQ(decoded[0], decoded[1]);
    EXPECT_EQ(decoded[0], decoded[2]);
    EXPECT_EQ(decoded[3], 128U);
}

/// @brief Blend in linear space before storing sRGB bytes and linear alpha.
TEST_F(pass_test, srgb_attachment_blends_in_linear_space)
{
    target =
        resources.create_texture({16, 16, texture_role::color_target, texture_format::rgba8_srgb});
    triangle({1, 1, 1, 0.5F});
    const std::array invalid{pass()};
    EXPECT_THROW(renderer.submit(frame_plan{invalid}), std::invalid_argument);
    EXPECT_FALSE(resources.ready(target));
    EXPECT_EQ(resources.state(target), resource_state::copy_destination);

    pipeline_description description;
    description.blend = blend_mode::straight_alpha;
    description.color_format = texture_format::rgba8_srgb;
    pipeline = renderer.create_pipeline(description);
    auto draw = pass();
    draw.color.clear_color[3] = 0;
    const std::array passes{draw};
    renderer.submit(frame_plan{passes});
    const auto result = pixel(4, 4);
    EXPECT_NEAR(result[0], 188, 1);
    EXPECT_EQ(result[0], result[1]);
    EXPECT_EQ(result[0], result[2]);
    EXPECT_NEAR(result[3], 128, 1);
}

/// @brief Coverage uses R8 as opacity and supports premultiplied blending with straight tint.
TEST_F(pass_test, atlas_coverage_matches_straight_and_premultiplied_blend)
{
    triangle({1, 0, 0, 0.5F});
    const auto atlas =
        resources.create_texture({1, 1, texture_role::sampled, texture_format::r8_unorm});
    const std::array texel{std::byte{128}};
    resources.upload(atlas, texel);
    const auto view = resources.create_view(atlas);
    pipeline_description description;
    description.program = shader_program::coverage;
    for (const auto blend : {blend_mode::straight_alpha, blend_mode::premultiplied_alpha})
    {
        description.blend = blend;
        pipeline = renderer.create_pipeline(description);
        auto draw = pass();
        draw.color.clear_color[3] = 0;
        draw.draws.front().texture = view;
        const std::array passes{draw};
        renderer.submit(frame_plan{passes});
        EXPECT_EQ(pixel(4, 4), (std::array<unsigned, 4>{64, 0, 0, 64}));
    }
}

/// @brief Odd mip extents round down and each tight level is independently observable.
TEST_F(resource_test, mip_upload_is_atomic_and_readback_has_tight_rows)
{
    const auto atlas =
        resources.create_texture({7, 3, texture_role::sampled, texture_format::r8_unorm, 3});
    std::array<std::byte, 25> texels{};
    for (std::size_t index = 0; index < texels.size(); ++index)
    {
        texels.at(index) = static_cast<std::byte>(index);
    }

    EXPECT_THROW(resources.upload(atlas, std::span{texels}.first(24)), std::invalid_argument);
    EXPECT_FALSE(resources.ready(atlas));
    EXPECT_EQ(resources.statistics().pending_transfers, 0U);
    resources.wait(resources.upload(atlas, texels));
    constexpr std::array<std::size_t, 4> offsets{0, 21, 24, 25};
    for (std::uint16_t mip = 0; mip < 3; ++mip)
    {
        const auto bytes = resources.readback(atlas, mip);
        EXPECT_TRUE(std::ranges::equal(
            bytes,
            std::span{texels}.subspan(offsets.at(mip), offsets.at(mip + 1) - offsets.at(mip))));
    }

    EXPECT_THROW(resources.readback(atlas, 3), std::invalid_argument);
    EXPECT_EQ(resources.state(atlas), resource_state::shader_read);
}

/// @brief Cancellation retires both atlas handles but keeps the queue's staging alive.
TEST_F(resource_test, retiring_mipped_atlas_preserves_allocation_and_descriptor_until_fence)
{
    gpu_gate gate{graphics};
    const auto atlas =
        resources.create_texture({7, 3, texture_role::sampled, texture_format::r8_unorm, 3});
    const auto view = resources.create_view(atlas);
    const std::array<std::byte, 25> source{};
    const auto fence = resources.upload(atlas, source);
    resources.release(atlas);
    resources.release(view);
    resources.collect();
    EXPECT_THROW(resources.create_view(atlas), std::invalid_argument);
    EXPECT_EQ(resources.statistics().retired_resources, 1U);
    EXPECT_EQ(resources.statistics().retired_descriptors, 1U);
    EXPECT_GT(resources.statistics().staging_bytes, source.size());
    gate.open();
    resources.wait(fence);
    EXPECT_EQ(resources.statistics().retired_resources, 0U);
    EXPECT_EQ(resources.statistics().retired_descriptors, 0U);
    EXPECT_EQ(resources.statistics().staging_bytes, 0U);
}

/// @brief Admission charges native padding, preserving the untouched texture on failure.
TEST_F(resource_test, rejects_mip_staging_that_exceeds_aligned_budget)
{
    const auto atlas =
        resources.create_texture({32, 32, texture_role::sampled, texture_format::r8_unorm, 2});
    const std::array<std::byte, 1280> texels{};
    EXPECT_THROW(resources.upload(atlas, texels), std::length_error);
    EXPECT_EQ(resources.state(atlas), resource_state::copy_destination);
    EXPECT_FALSE(resources.ready(atlas));
    EXPECT_EQ(resources.statistics().pending_transfers, 0U);
    EXPECT_EQ(resources.statistics().staging_bytes, 0U);
}

/// @brief A texture view exposes the uploaded tail through the existing custom shader ABI.
TEST_F(pass_test, custom_shader_samples_non_base_mip)
{
    triangle({1, 1, 1, 1});
    const auto atlas =
        resources.create_texture({3, 2, texture_role::sampled, texture_format::r8_unorm, 2});
    std::array<std::byte, 7> texels{};
    texels.back() = std::byte{192};
    resources.upload(atlas, texels);
    const auto view = resources.create_view(atlas);
    pipeline_description description;
    description.program = shader_program::custom;
    description.source = R"(
        Texture2D<float4> atlas : register(t0);
        SamplerState clamp_sampler : register(s0);
        float4 vertex_main(float3 p : POSITION) : SV_Position { return float4(p, 1); }
        float4 pixel_main() : SV_Target {
            return float4(atlas.SampleLevel(clamp_sampler, float2(0.5, 0.5), 1).r, 0, 0, 1);
        })";
    pipeline = renderer.create_pipeline(description);
    auto draw = pass();
    draw.draws.front().texture = view;
    const std::array passes{draw};
    renderer.submit(frame_plan{passes});
    EXPECT_EQ(pixel(4, 4), (std::array<unsigned, 4>{192, 0, 0, 255}));
}
} // namespace atlas::renderer::tests
