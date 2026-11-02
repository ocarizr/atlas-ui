/** @file @brief Controlled DXGI occlusion prevents redundant submissions and resumes cleanly. */
#include "native_renderer.hpp"
#include <atlas/window/window.hpp>
#include <gtest/gtest.h>

namespace atlas::renderer::tests
{
/// @brief Actual D3D12 commands with deterministic presentation statuses instead of desktop timing.
class scripted_surface final : public detail::native_surface
{
  public:
    using native_surface::native_surface; ///< Preserve native initialization/lifetime.
    bool visible = false;   ///< Controlled visibility independent of hosted desktop composition.
    std::size_t probes = 0; ///< Non-rendering visibility probes.

    HRESULT present_native(bool probe) override
    {
        if (probe)
        {
            ++probes;
        }

        return visible ? S_OK : DXGI_STATUS_OCCLUDED;
    } ///< Only this boundary is simulated; recorded commands and fences are real.
};

/// @brief Repeated occlusion polls never advance the graphics submission count.
TEST(presentation_occlusion, probes_without_recording_until_visible)
{
    window::window target{{"Occlusion", 240, 180, false}};
    device graphics{{adapter_preference::software}};
    auto context =
        std::static_pointer_cast<detail::native_device>(detail::device_access::retain(graphics));
    scripted_surface surface{context, window::detail::window_access::retain(target), {}};
    EXPECT_EQ(surface.render({}), frame_result::occluded);
    EXPECT_EQ(surface.submitted(), 1U);
    EXPECT_EQ(surface.render({}), frame_result::occluded);
    EXPECT_EQ(surface.submitted(), 1U);
    EXPECT_EQ(surface.probes, 1U);
    surface.visible = true;
    EXPECT_EQ(surface.render({}), frame_result::presented);
    EXPECT_EQ(surface.submitted(), 2U);
    EXPECT_EQ(surface.probes, 2U);
    graphics.wait_idle();
    EXPECT_EQ(graphics.debug_error_count(), 0U);
}
} // namespace atlas::renderer::tests
