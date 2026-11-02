/** @file @brief Isolated generic pass fixtures and pixel readback helpers. */
#pragma once
#include "resource_test_support.hpp"
#include <atlas/renderer/passes.hpp>
#include <atlas/window/window.hpp>
#include <cstring>

namespace atlas::renderer::tests
{
/// @brief Each case owns independent native resources and validates the debug queue.
class pass_test : public ::testing::Test
{
  protected:
    device graphics{{adapter_preference::software}}; ///< Deterministic software device.
    resource_store resources{graphics};              ///< Shared registry.
    pass_renderer renderer{resources};               ///< Generic pass submission.
    texture_handle target =
        resources.create_texture({16, 16, texture_role::color_target}); ///< Offscreen output.
    buffer_handle geometry{};   ///< Optional full-screen triangle.
    pipeline_handle pipeline{}; ///< Default colored state.

    /// @brief Upload a large triangle covering the attachment.
    void triangle(std::array<float, 4> color = {1, 0, 0, 1}, float z = 0.5F)
    {
        const std::array<vertex, 3> vertices{
            {{{-1, -1, z}, color, {}}, {{-1, 3, z}, color, {}}, {{3, -1, z}, color, {}}}};
        geometry = resources.create_buffer({sizeof(vertices)});
        resources.upload(geometry, std::as_bytes(std::span{vertices}));
        pipeline = renderer.create_pipeline({});
    }

    /// @brief Build one ordered draw or a clear-only pass.
    pass_description pass() const
    {
        pass_description result;
        result.color.target = target;
        if (geometry.owner != 0)
        {
            draw_packet draw;
            draw.pipeline = pipeline;
            draw.vertices = geometry;
            draw.vertex_count = 3;
            result.draws.push_back(draw);
        }

        return result;
    }

    /// @brief Read one tight RGBA pixel after queue completion.
    std::array<unsigned, 4> pixel(std::uint32_t x, std::uint32_t y)
    {
        const auto bytes = resources.readback(target);
        const auto offset = (static_cast<std::size_t>(y) * 16 + x) * 4;
        return {std::to_integer<unsigned>(bytes[offset]),
                std::to_integer<unsigned>(bytes[offset + 1]),
                std::to_integer<unsigned>(bytes[offset + 2]),
                std::to_integer<unsigned>(bytes[offset + 3])};
    }

    /// @brief Drain before examining validation errors.
    void TearDown() override
    {
        graphics.wait_idle();
        EXPECT_EQ(graphics.debug_error_count(), 0U);
    }
};

} // namespace atlas::renderer::tests
