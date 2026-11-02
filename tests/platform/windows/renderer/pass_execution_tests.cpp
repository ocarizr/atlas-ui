/** @file @brief Pixel-level WARP validation of generic ordered render passes. */
#include "pass_test_support.hpp"

namespace atlas::renderer::tests
{
/// @brief Rendered attachments become initialized readable GPU data.
TEST_F(pass_test, clears_and_draws_offscreen_triangle)
{
    triangle();
    const std::array passes{pass()};
    resources.wait(renderer.submit(frame_plan{passes}));
    EXPECT_EQ(pixel(8, 8), (std::array<unsigned, 4>{255, 0, 0, 255}));
}

/// @brief Scissor preserves the clear color outside the selected pixel rectangle.
TEST_F(pass_test, scissor_clips_draw)
{
    triangle();
    auto description = pass();
    description.draws[0].scissor = pixel_rectangle{0, 0, 8, 16};
    const std::array passes{description};
    renderer.submit(frame_plan{passes});
    EXPECT_EQ(pixel(2, 8)[0], 255U);
    EXPECT_EQ(pixel(12, 8)[0], 0U);
}

/// @brief Invalid later packets never submit valid earlier passes.
TEST_F(pass_test, whole_frame_rejection_has_no_gpu_or_resource_side_effects)
{
    auto first = pass();
    auto second = first;
    second.color.target.generation++;
    const auto before = detail::device_access::retain(graphics);
    const auto native = std::static_pointer_cast<detail::native_device>(before);
    const auto fence = native->next_fence;
    const std::array passes{first, second};
    EXPECT_THROW(renderer.submit(frame_plan{passes}), std::invalid_argument);
    EXPECT_EQ(native->next_fence, fence);
    EXPECT_FALSE(resources.ready(target));
    EXPECT_EQ(resources.state(target), resource_state::copy_destination);
}

/// @brief Loads cannot read never initialized attachment data.
TEST_F(pass_test, rejects_uninitialized_load)
{
    auto description = pass();
    description.color.load = load_action::load;
    const std::array passes{description};
    EXPECT_THROW(renderer.submit(frame_plan{passes}), std::logic_error);
}

/// @brief Shader compilation errors do not consume a reusable pipeline slot.
TEST_F(pass_test, compilation_failure_does_not_publish_pipeline)
{
    pipeline_description description;
    description.program = shader_program::custom;
    description.source = "invalid hlsl";
    EXPECT_THROW(renderer.create_pipeline(description), std::runtime_error);
    EXPECT_NE(renderer.create_pipeline({}).owner, 0U);
}

/// @brief A stored target can be sampled by a later pass without a CPU wait.
TEST_F(pass_test, samples_previous_pass_output_in_submission_order)
{
    triangle({1, 1, 1, 1});
    const auto source = resources.create_texture({16, 16, texture_role::color_target});
    auto first = pass();
    first.color.target = source;
    first.color.clear_color = {0, 1, 0, 1};
    first.draws.clear();
    auto second = pass();
    pipeline_description textured;
    textured.program = shader_program::textured;
    second.draws[0].pipeline = renderer.create_pipeline(textured);
    second.draws[0].texture = resources.create_view(source);
    const std::array passes{first, second};
    renderer.submit(frame_plan{passes});
    EXPECT_EQ(pixel(8, 8), (std::array<unsigned, 4>{0, 255, 0, 255}));
}

/// @brief Discarded contents cannot satisfy a later pass's load contract.
TEST_F(pass_test, rejects_load_after_discard_before_submission)
{
    auto first = pass();
    first.color.store = store_action::discard;
    auto second = pass();
    second.color.load = load_action::load;
    const std::array passes{first, second};
    EXPECT_THROW(renderer.submit(frame_plan{passes}), std::logic_error);
    EXPECT_FALSE(resources.ready(target));
}

/// @brief The same allocation cannot be read through an SRV while used as a color output.
TEST_F(pass_test, rejects_attachment_feedback)
{
    triangle();
    pipeline_description textured;
    textured.program = shader_program::textured;
    auto description = pass();
    description.draws[0].pipeline = renderer.create_pipeline(textured);
    description.draws[0].texture = resources.create_view(target);
    const std::array passes{description};
    EXPECT_THROW(renderer.submit(frame_plan{passes}), std::invalid_argument);
}

/// @brief Alpha composition preserves destination contribution.
TEST_F(pass_test, blends_straight_alpha)
{
    triangle({1, 0, 0, 0.5F});
    pipeline_description alpha;
    alpha.blend = blend_mode::straight_alpha;
    auto description = pass();
    description.color.clear_color = {0, 0, 1, 1};
    description.draws[0].pipeline = renderer.create_pipeline(alpha);
    const std::array passes{description};
    renderer.submit(frame_plan{passes});
    const auto value = pixel(8, 8);
    EXPECT_NEAR(value[0], 128, 1);
    EXPECT_NEAR(value[2], 128, 1);
    EXPECT_EQ(value[3], 255U);
}

/// @brief Depth testing rejects geometry behind a nearer initialized depth value.
TEST_F(pass_test, depth_attachment_rejects_occluded_geometry)
{
    triangle();
    pipeline_description depth;
    depth.depth = depth_mode::read_write;
    auto description = pass();
    description.depth = depth_attachment{
        resources.create_texture({16, 16, texture_role::depth_target}), load_action::clear, 0.25F};
    description.draws[0].pipeline = renderer.create_pipeline(depth);
    const std::array passes{description};
    renderer.submit(frame_plan{passes});
    EXPECT_EQ(pixel(8, 8), (std::array<unsigned, 4>{0, 0, 0, 255}));
    EXPECT_THROW(resources.create_view(description.depth->target), std::invalid_argument);
}

/// @brief A nearer primitive writes depth, rejecting a later farther primitive.
TEST_F(pass_test, depth_writes_preserve_near_geometry)
{
    triangle({1, 0, 0, 1}, 0.25F);
    const auto near_geometry = geometry;
    triangle({0, 0, 1, 1}, 0.75F);
    pipeline_description depth;
    depth.depth = depth_mode::read_write;
    auto description = pass();
    description.depth =
        depth_attachment{resources.create_texture({16, 16, texture_role::depth_target})};
    description.draws[0].pipeline = renderer.create_pipeline(depth);
    auto first = description.draws[0];
    first.vertices = near_geometry;
    description.draws.insert(description.draws.begin(), first);
    const std::array passes{description};
    renderer.submit(frame_plan{passes});
    EXPECT_EQ(pixel(8, 8), (std::array<unsigned, 4>{255, 0, 0, 255}));
}

/// @brief A later color load preserves existing data while composing premultiplied color.
TEST_F(pass_test, loads_previous_color_and_blends_premultiplied_alpha)
{
    triangle({0.5F, 0, 0, 0.5F});
    auto first = pass();
    first.draws.clear();
    first.color.clear_color = {0, 0, 1, 1};
    auto second = pass();
    second.color.load = load_action::load;
    pipeline_description alpha;
    alpha.blend = blend_mode::premultiplied_alpha;
    second.draws[0].pipeline = renderer.create_pipeline(alpha);
    const std::array passes{first, second};
    renderer.submit(frame_plan{passes});
    const auto value = pixel(8, 8);
    EXPECT_NEAR(value[0], 128, 1);
    EXPECT_NEAR(value[2], 128, 1);
    EXPECT_EQ(value[3], 255U);
}

/// @brief Successful discard invalidates subsequent readiness and diagnostic readback.
TEST_F(pass_test, discard_invalidates_stored_contents)
{
    auto description = pass();
    description.color.store = store_action::discard;
    const std::array passes{description};
    resources.wait(renderer.submit(frame_plan{passes}));
    EXPECT_FALSE(resources.ready(target));
    EXPECT_THROW(resources.readback(target), std::logic_error);
}

/// @brief A viewport limits transformed primitive placement independently of scissor.
TEST_F(pass_test, viewport_limits_draw_region)
{
    triangle();
    auto description = pass();
    description.draws[0].viewport = pixel_viewport{0, 0, 8, 16};
    const std::array passes{description};
    renderer.submit(frame_plan{passes});
    EXPECT_EQ(pixel(2, 8)[0], 255U);
    EXPECT_EQ(pixel(12, 8)[0], 0U);
}

/// @brief Copy rendered output to a real swapchain without a CPU readback upload cycle.
TEST_F(pass_test, presents_rendered_target_and_preserves_source_state)
{
    window::window window{{"Pass output", 160, 120, false}};
    const auto extent = window.state();
    resources.release(target);
    resources.collect();
    target = resources.create_texture({extent.width, extent.height, texture_role::color_target});
    triangle();
    const std::array passes{pass()};
    renderer.submit(frame_plan{passes});
    presentation_surface surface{graphics, window, {2, false}};
    const auto result = surface.present(resources, target);
    EXPECT_TRUE(result == frame_result::presented || result == frame_result::occluded);
    const auto captured = surface.readback_last_frame();
    EXPECT_EQ(captured.rgba[(static_cast<std::size_t>(8) * extent.width + 8) * 4], 255U);
    EXPECT_EQ(resources.state(target), resource_state::render_target);
}

/// @brief Pipeline and resource retirement cannot reuse storage while a queued frame uses it.
TEST_F(pass_test, retains_resources_and_pipeline_until_fence_completion)
{
    pass_renderer bounded{resources, {1, 2, 8, 1}};
    triangle();
    auto description = pass();
    const auto held = bounded.create_pipeline({});
    description.draws[0].pipeline = held;
    const std::array passes{description};
    gpu_gate gate{graphics};
    const auto done = bounded.submit(frame_plan{passes});
    bounded.release(held);
    resources.release(geometry);
    resources.release(target);
    bounded.collect();
    resources.collect();
    EXPECT_THROW(bounded.create_pipeline({}), std::overflow_error);
    EXPECT_THROW(bounded.submit(frame_plan{passes}), std::invalid_argument);
    EXPECT_EQ(resources.statistics().retired_resources, 2U);
    gate.open();
    resources.wait(done);
    bounded.collect();
    EXPECT_EQ(resources.statistics().retired_resources, 0U);
    const auto reused = bounded.create_pipeline({});
    EXPECT_EQ(reused.index, held.index);
    EXPECT_NE(reused.generation, held.generation);
}
} // namespace atlas::renderer::tests
