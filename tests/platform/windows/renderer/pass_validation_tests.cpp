/** @file @brief Rejected draw packets and bounded pass admission preserve queue state. */
#include "pass_test_support.hpp"
#include <future>
#include <limits>

namespace atlas::renderer::tests
{
/// @brief Named independent input mutations sharing the same validity baseline.
enum class invalid_draw_case : std::uint8_t
{
    stale_pipeline,      ///< Invalid pipeline generation.
    stale_vertices,      ///< Invalid vertex generation.
    empty_draw,          ///< Empty triangle list.
    incomplete_triangle, ///< Incomplete primitive.
    oversized_draw,      ///< Buffer bounds exceeded.
    oversized_first,     ///< Starting vertex beyond storage.
    nonfinite_transform, ///< Nonfinite transform coefficient.
    nonfinite_tint,      ///< Nonfinite color multiplier.
    reversed_scissor,    ///< Reversed pixel rectangle.
    oversized_scissor,   ///< Scissor exceeds attachment.
    invalid_viewport,    ///< Negative or nonfinite viewport.
    unexpected_texture,  ///< Colored pipeline consumes no texture.
    incompatible_depth   ///< Pipeline and pass depth formats differ.
};

/// @brief One isolated device per invalid input variation.
class invalid_draw_test : public pass_test, public ::testing::WithParamInterface<invalid_draw_case>
{
};

/// @brief Invalid packets are rejected before queue signals or attachment initialization.
TEST_P(invalid_draw_test, rejects_without_submission)
{
    triangle();
    auto description = pass();
    auto& draw = description.draws[0];
    switch (GetParam())
    {
        case invalid_draw_case::stale_pipeline:
            ++draw.pipeline.generation;
            break;
        case invalid_draw_case::stale_vertices:
            ++draw.vertices.generation;
            break;
        case invalid_draw_case::empty_draw:
            draw.vertex_count = 0;
            break;
        case invalid_draw_case::incomplete_triangle:
            draw.vertex_count = 2;
            break;
        case invalid_draw_case::oversized_draw:
            draw.vertex_count = 6;
            break;
        case invalid_draw_case::oversized_first:
            draw.first_vertex = UINT32_MAX;
            break;
        case invalid_draw_case::nonfinite_transform:
            draw.transform[0] = std::numeric_limits<float>::infinity();
            break;
        case invalid_draw_case::nonfinite_tint:
            draw.tint[0] = std::numeric_limits<float>::quiet_NaN();
            break;
        case invalid_draw_case::reversed_scissor:
            draw.scissor = pixel_rectangle{8, 0, 2, 16};
            break;
        case invalid_draw_case::oversized_scissor:
            draw.scissor = pixel_rectangle{0, 0, 17, 16};
            break;
        case invalid_draw_case::invalid_viewport:
            draw.viewport = pixel_viewport{-1, 0, 16, 16};
            break;
        case invalid_draw_case::unexpected_texture:
            draw.texture = resources.create_view(target);
            break;
        case invalid_draw_case::incompatible_depth:
            description.depth =
                depth_attachment{resources.create_texture({16, 16, texture_role::depth_target})};
            break;
    }

    const auto native =
        std::static_pointer_cast<detail::native_device>(detail::device_access::retain(graphics));
    const auto before = native->next_fence;
    const std::array passes{description};
    EXPECT_THROW(renderer.submit(frame_plan{passes}), std::invalid_argument);
    EXPECT_EQ(native->next_fence, before);
    EXPECT_FALSE(resources.ready(target));
}

INSTANTIATE_TEST_SUITE_P(
    invalid_packets, invalid_draw_test,
    ::testing::Values(invalid_draw_case::stale_pipeline, invalid_draw_case::stale_vertices,
                      invalid_draw_case::empty_draw, invalid_draw_case::incomplete_triangle,
                      invalid_draw_case::oversized_draw, invalid_draw_case::oversized_first,
                      invalid_draw_case::nonfinite_transform, invalid_draw_case::nonfinite_tint,
                      invalid_draw_case::reversed_scissor, invalid_draw_case::oversized_scissor,
                      invalid_draw_case::invalid_viewport, invalid_draw_case::unexpected_texture,
                      invalid_draw_case::incompatible_depth));

/// @brief Frame admission fails without waiting when the bounded queue is occupied.
TEST_F(pass_test, rejects_frame_capacity_then_reuses_completed_slot)
{
    pass_renderer bounded{resources, {1, 1, 1, 1}};
    const std::array passes{pass()};
    gpu_gate gate{graphics};
    const auto first = bounded.submit(frame_plan{passes});
    EXPECT_THROW(bounded.submit(frame_plan{passes}), std::overflow_error);
    gate.open();
    resources.wait(first);
    EXPECT_NO_THROW(resources.wait(bounded.submit(frame_plan{passes})));
}

/// @brief Every public operation rejects execution outside its device coordinator.
TEST_F(pass_test, rejects_foreign_thread)
{
    const std::array passes{pass()};
    auto result =
        std::async(std::launch::async,
                   [&]
                   {
                       EXPECT_THROW(renderer.create_pipeline({}), std::logic_error);
                       EXPECT_THROW(renderer.collect(), std::logic_error);
                       EXPECT_THROW(renderer.submit(frame_plan{passes}), std::logic_error);
                   });
    result.get();
}

/// @brief Pipeline identity never crosses a renderer registry even on the same device.
TEST_F(pass_test, rejects_foreign_pipeline_and_stale_descriptor)
{
    triangle();
    pass_renderer other{resources};
    auto description = pass();
    description.draws[0].pipeline = other.create_pipeline({});
    const std::array foreign{description};
    EXPECT_THROW(renderer.submit(frame_plan{foreign}), std::invalid_argument);
    pipeline_description textured;
    textured.program = shader_program::textured;
    description.draws[0].pipeline = renderer.create_pipeline(textured);
    description.draws[0].texture = resources.create_view(target);
    resources.release(description.draws[0].texture);
    const std::array stale{description};
    EXPECT_THROW(renderer.submit(frame_plan{stale}), std::invalid_argument);
}
} // namespace atlas::renderer::tests
