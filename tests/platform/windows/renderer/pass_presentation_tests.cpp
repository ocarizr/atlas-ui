/** @file @brief Presentation rejection and suspension never submit partial GPU work. */
#include "pass_test_support.hpp"

namespace atlas::renderer::tests
{
/// @brief Neither uninitialized input nor a mismatched extent can reach a copy command.
TEST_F(pass_test, presentation_rejects_uninitialized_and_mismatched_source)
{
    window::window window{{"Presentation inputs", 160, 120, false}};
    presentation_surface surface{graphics, window, {2, false}};
    EXPECT_THROW(surface.present(resources, target), std::invalid_argument);
    const std::array passes{pass()};
    resources.wait(renderer.submit(frame_plan{passes}));
    const auto native =
        std::static_pointer_cast<detail::native_device>(detail::device_access::retain(graphics));
    const auto before = native->next_fence;
    EXPECT_THROW(surface.present(resources, target), std::invalid_argument);
    EXPECT_EQ(native->next_fence, before);
    EXPECT_EQ(surface.submitted_frames(), 0U);
}

/// @brief Device provenance is checked before native resource resolution.
TEST_F(pass_test, presentation_rejects_foreign_device)
{
    const std::array passes{pass()};
    resources.wait(renderer.submit(frame_plan{passes}));
    device other{{adapter_preference::software}};
    window::window window{{"Foreign device", 160, 120, false}};
    presentation_surface surface{other, window, {2, false}};
    EXPECT_THROW(surface.present(resources, target), std::invalid_argument);
    EXPECT_EQ(surface.submitted_frames(), 0U);
    other.wait_idle();
    EXPECT_EQ(other.debug_error_count(), 0U);
}

/// @brief Zero extents and a native close are non-submitting presentation outcomes.
TEST_F(pass_test, presentation_skips_suspended_and_closed_surface)
{
    const std::array passes{pass()};
    resources.wait(renderer.submit(frame_plan{passes}));
    window::window window{{"Suspended copy", 160, 120, false}};
    presentation_surface surface{graphics, window, {2, false}};
    surface.resize({0, 0});
    const auto native =
        std::static_pointer_cast<detail::native_device>(detail::device_access::retain(graphics));
    const auto before = native->next_fence;
    EXPECT_EQ(surface.present(resources, target), frame_result::suspended);
    window.request_close();
    EXPECT_EQ(surface.present(resources, target), frame_result::surface_closed);
    EXPECT_EQ(surface.submitted_frames(), 0U);
    EXPECT_EQ(native->next_fence, before);
}

/// @brief Depth contents cannot be copied into an RGBA swapchain through the public API.
TEST_F(pass_test, presentation_rejects_depth_target)
{
    auto description = pass();
    const auto depth = resources.create_texture({16, 16, texture_role::depth_target});
    description.depth = depth_attachment{depth};
    const std::array passes{description};
    resources.wait(renderer.submit(frame_plan{passes}));
    window::window window{{"Depth input", 160, 120, false}};
    presentation_surface surface{graphics, window, {2, false}};
    EXPECT_THROW(surface.present(resources, depth), std::invalid_argument);
}
} // namespace atlas::renderer::tests
