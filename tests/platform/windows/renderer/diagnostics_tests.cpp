/** @file @brief Native graphics CPU scopes are independently injectable and exportable. */
#include <atlas/renderer/passes.hpp>
#include <atlas/renderer/renderer.hpp>
#include <atlas/window/window.hpp>
#include <gtest/gtest.h>
#include <sstream>

namespace atlas::renderer::tests
{
/// @brief Valid and rejected frames preserve their behavior while optional capture records CPU
/// work.
TEST(renderer_diagnostics_tests, captures_frames_and_validation_failures)
{
    auto capture = std::make_shared<diagnostics::profiler>();
    {
        window::window target{{"Diagnostics", 80, 60, false}};
        device graphics{{adapter_preference::software}, {}, {{}, capture}};
        presentation_surface surface{graphics, target};
        surface.render_frame();
        EXPECT_THROW(surface.render_frame({{-1, 0, 0, 1}, false}), std::invalid_argument);
        EXPECT_EQ(surface.submitted_frames(), 1U);
        resource_store resources{graphics};
        pass_renderer renderer{resources};
        pass_description pass;
        const auto extent = target.state();
        pass.color.target =
            resources.create_texture({extent.width, extent.height, texture_role::color_target});
        const std::array passes{pass};
        resources.wait(renderer.submit(frame_plan{passes}));
        surface.present(resources, pass.color.target);
    }
    std::ostringstream trace;
    capture->write_chrome_trace(trace);
    EXPECT_EQ(trace.str().find("surface.frame") != std::string::npos,
              diagnostics::instrumentation_enabled);
    EXPECT_EQ(trace.str().find("surface.shutdown") != std::string::npos,
              diagnostics::instrumentation_enabled);
    EXPECT_EQ(trace.str().find("pass.submit") != std::string::npos,
              diagnostics::instrumentation_enabled);
    EXPECT_EQ(trace.str().find("surface.present_source") != std::string::npos,
              diagnostics::instrumentation_enabled);
    EXPECT_EQ(trace.str().find("\"result\":\"failed\"") != std::string::npos,
              diagnostics::instrumentation_enabled);
}
} // namespace atlas::renderer::tests
