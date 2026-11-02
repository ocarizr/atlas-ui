/** @file @brief Attachment initialization, compatibility, read-only depth and admission bounds. */
#include "pass_test_support.hpp"

namespace atlas::renderer::tests
{
/// @brief Explicit resource states must match the capabilities fixed at allocation.
TEST_F(pass_test, rejects_incompatible_states_and_depth_upload)
{
    triangle();
    const auto depth = resources.create_texture({16, 16, texture_role::depth_target});
    const auto sampled = resources.create_texture({16, 16});
    EXPECT_THROW(resources.transition(geometry, resource_state::render_target),
                 std::invalid_argument);
    EXPECT_THROW(resources.transition(sampled, resource_state::render_target),
                 std::invalid_argument);
    EXPECT_THROW(resources.transition(target, resource_state::depth_write), std::invalid_argument);
    EXPECT_THROW(resources.transition(depth, resource_state::shader_read), std::invalid_argument);
    const std::array<std::byte, std::size_t{16} * 16 * 4> bytes{};
    EXPECT_THROW(resources.upload(depth, bytes), std::invalid_argument);
}

/// @brief Color roles, matching depth extents and normalized clear values are validated first.
TEST_F(pass_test, rejects_invalid_attachments)
{
    auto description = pass();
    description.color.target = resources.create_texture({16, 16});
    const std::array sampled{description};
    EXPECT_THROW(renderer.submit(frame_plan{sampled}), std::invalid_argument);
    description = pass();
    description.depth =
        depth_attachment{resources.create_texture({8, 16, texture_role::depth_target})};
    const std::array mismatch{description};
    EXPECT_THROW(renderer.submit(frame_plan{mismatch}), std::invalid_argument);
    description.depth = depth_attachment{
        resources.create_texture({16, 16, texture_role::depth_target}), load_action::clear, 2};
    const std::array invalid_depth{description};
    EXPECT_THROW(renderer.submit(frame_plan{invalid_depth}), std::invalid_argument);
    description = pass();
    description.color.clear_color[0] = -1;
    const std::array invalid_color{description};
    EXPECT_THROW(renderer.submit(frame_plan{invalid_color}), std::invalid_argument);
    EXPECT_FALSE(resources.ready(target));
}

/// @brief Oversized frames fail on CPU without allocating unbounded native descriptor heaps.
TEST_F(pass_test, rejects_pass_and_draw_limits)
{
    triangle();
    pass_renderer bounded{resources, {1, 1, 1, 1}};
    auto description = pass();
    description.draws[0].pipeline = bounded.create_pipeline({});
    const std::array too_many_passes{description, description};
    EXPECT_THROW(bounded.submit(frame_plan{too_many_passes}), std::length_error);
    description.draws.push_back(description.draws[0]);
    const std::array too_many_draws{description};
    EXPECT_THROW(bounded.submit(frame_plan{too_many_draws}), std::length_error);
    EXPECT_FALSE(resources.ready(target));
}

/// @brief Read-only depth compares against the clear value without writing nearer geometry.
TEST_F(pass_test, read_only_depth_preserves_clear_value)
{
    triangle({1, 0, 0, 1}, 0.25F);
    const auto near_geometry = geometry;
    triangle({0, 0, 1, 1}, 0.75F);
    pipeline_description depth;
    depth.depth = depth_mode::read_only;
    auto description = pass();
    const auto attachment = resources.create_texture({16, 16, texture_role::depth_target});
    description.depth = depth_attachment{attachment};
    description.draws[0].pipeline = renderer.create_pipeline(depth);
    auto first = description.draws[0];
    first.vertices = near_geometry;
    description.draws.insert(description.draws.begin(), first);
    const std::array passes{description};
    renderer.submit(frame_plan{passes});
    EXPECT_EQ(pixel(8, 8), (std::array<unsigned, 4>{0, 0, 255, 255}));
    const auto bytes = resources.readback(attachment);
    float stored = 0;
    std::memcpy(&stored, bytes.data(), sizeof(stored));
    EXPECT_FLOAT_EQ(stored, 1);
}
} // namespace atlas::renderer::tests
