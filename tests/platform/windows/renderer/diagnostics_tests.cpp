/** @file @brief Native graphics CPU scopes are independently injectable and exportable. */
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
    }
    std::ostringstream trace;
    capture->write_chrome_trace(trace);
    EXPECT_EQ(trace.str().find("surface.frame") != std::string::npos,
              diagnostics::instrumentation_enabled);
    EXPECT_EQ(trace.str().find("surface.shutdown") != std::string::npos,
              diagnostics::instrumentation_enabled);
    EXPECT_EQ(trace.str().find("\"result\":\"failed\"") != std::string::npos,
              diagnostics::instrumentation_enabled);
}
} // namespace atlas::renderer::tests
