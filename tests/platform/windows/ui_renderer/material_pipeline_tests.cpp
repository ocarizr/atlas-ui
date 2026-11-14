/** @file @brief Reference pixels and bounded native registration for authored material shaders. */
#include <atlas/ui_renderer/paint_renderer.hpp>
#include <gtest/gtest.h>

namespace atlas::ui_renderer::tests
{
namespace
{
constexpr std::string_view shape_source = R"(
cbuffer material : register(b1) { float4 a; float4 b; float4 c; float4 d; };
float4 atlas_material(float4 color, float2 uv) {
    return float4(color.r * a.x, color.g * d.x, 0, color.a);
}
)"; ///< Maps two stable slots to visibly distinct linear channels.

constexpr std::string_view sampled_source = R"(
cbuffer material : register(b1) { float4 region; };
Texture2D<float4> image : register(t0);
SamplerState clamp_sampler : register(s0);
float4 atlas_material(float4 color, float2 uv) {
    return image.Sample(clamp_sampler, uv * region.zw + region.xy) * color;
}
)"; ///< Matches the texture parameter ABI exactly.

constexpr std::string_view mismatched_source = R"(
cbuffer material : register(b1) { float4 a; };
float4 atlas_material(float4 color, float2 uv) { return a; }
)"; ///< Valid HLSL, incompatible with either strict material family.

/// @brief Build a full-screen quad with a caller-supplied inherited rectangular scissor.
ui::paint_quad quad(ui::layout_rect clip, std::array<float, 4> color)
{
    ui::paint_quad result;
    result.clip = clip;
    result.bounds = {0, 0, 16, 16};
    constexpr std::array<ui::layout_point, 6> corners{
        {{0, 0}, {1, 0}, {0, 1}, {0, 1}, {1, 0}, {1, 1}}};
    for (std::size_t index = 0; index < corners.size(); ++index)
    {
        const auto coord = corners.at(index);
        result.vertices.at(index) = {{16 * coord.x, 16 * coord.y}, coord, color};
    }

    return result;
}

/// @brief Use a tight pipeline limit to exercise rollback and retirement under pressure.
class authored_material_test : public testing::Test
{
  protected:
    renderer::device graphics{{renderer::adapter_preference::software}}; ///< Deterministic WARP.
    renderer::resource_store resources{graphics}; ///< Own resources through GPU completion.
    renderer::pass_renderer passes{resources, {.max_pipelines = 3}}; ///< Only one spare PSO.
    paint_renderer painter{resources, passes}; ///< Reusable material renderer.
    renderer::texture_handle target = resources.create_texture(
        {16, 16, renderer::texture_role::color_target}); ///< Reference output.

    /// @brief Read one tight RGBA reference pixel.
    std::array<unsigned, 4> pixel(std::size_t x, std::size_t y)
    {
        const auto bytes = resources.readback(target);
        const auto offset = (y * 16 + x) * 4;
        return {std::to_integer<unsigned>(bytes.at(offset)),
                std::to_integer<unsigned>(bytes.at(offset + 1)),
                std::to_integer<unsigned>(bytes.at(offset + 2)),
                std::to_integer<unsigned>(bytes.at(offset + 3))};
    }

    /// @brief Drain before inspecting native validation messages.
    void TearDown() override
    {
        graphics.wait_idle();
        EXPECT_EQ(graphics.debug_error_count(), 0U);
    }
};
} // namespace

/// @brief Validate actual pixel reflection and preserve painter order, alpha and scissor.
TEST_F(authored_material_test, ShapeShaderKeepsClipBlendAndRetirement)
{
    constexpr auto shape = ui::material_kind::shape;
    EXPECT_THROW(painter.register_shader({1, shape, mismatched_source}), std::invalid_argument);
    painter.register_shader({1, shape, shape_source});
    EXPECT_THROW(painter.register_shader({1, shape, shape_source}), std::invalid_argument);
    EXPECT_THROW(painter.register_shader({2, shape, shape_source}), std::overflow_error);
    auto foreground = quad({0, 0, 8, 16}, {1, 0, 0, 0.5F});
    foreground.material = ui::material_style{.kind = shape, .radius = {1, 0, 0, 0}, .shader = 1};
    const std::array ordered{quad({0, 0, 16, 16}, {0, 0, 1, 1}), foreground};
    const paint_list packet{{0, 0, 16, 16}, ordered};
    const auto submitted = painter.submit_quads(packet, target, {16, 16});
    painter.release_shader(1);
    EXPECT_THROW(painter.submit_quads(packet, target, {16, 16}), std::invalid_argument);
    resources.wait(submitted);
    const auto blended = pixel(4, 4);
    EXPECT_NEAR(blended.at(0), 128, 1);
    EXPECT_NEAR(blended.at(2), 128, 1);
    EXPECT_EQ(pixel(12, 4), (std::array<unsigned, 4>{0, 0, 255, 255}));
    EXPECT_NO_THROW(painter.register_shader({2, shape, shape_source}));
}

/// @brief A custom sampled template uses the actual typed texture view and its lifetime checks.
TEST_F(authored_material_test, SampledShaderUsesSrgbViewAndRejectsRetiredDescriptor)
{
    constexpr auto kind = ui::material_kind::texture;
    EXPECT_THROW(painter.register_shader({1, kind, mismatched_source}), std::invalid_argument);
    painter.register_shader({1, kind, sampled_source});
    const auto image = resources.create_texture(
        {1, 1, renderer::texture_role::sampled, renderer::texture_format::rgba8_srgb});
    const std::array source{std::byte{128}, std::byte{128}, std::byte{128}, std::byte{255}};
    resources.upload(image, source);
    const auto view = resources.create_view(image);
    auto material = quad({0, 0, 16, 16}, {1, 1, 1, 1});
    material.texture = 2;
    material.material = ui::material_style{.shader = 1};
    const std::array quads{material};
    const paint_list packet{{0, 0, 16, 16}, quads};
    const std::array bindings{texture_binding{2, view}};
    resources.wait(painter.submit_quads(packet, target, {16, 16}, bindings));
    EXPECT_NEAR(pixel(4, 4).at(0), 55, 1);
    resources.release(view);
    EXPECT_THROW(painter.submit_quads(packet, target, {16, 16}, bindings), std::invalid_argument);
}
} // namespace atlas::ui_renderer::tests
