/** @file @brief GPU queries are consumed only after fences and obey optional capture compilation.
 */
#include "resource_test_support.hpp"
#include <atlas/renderer/passes.hpp>
#include <sstream>

namespace atlas::renderer::tests
{
/// @brief A CPU join never makes pending GPU timestamp/readback data available.
TEST(gpu_timing, publishes_fenced_duration_only_with_capture_enabled)
{
    auto capture = std::make_shared<diagnostics::profiler>();
    device graphics{{adapter_preference::software}, {}, {{}, capture}};
    resource_store resources{graphics};
    pass_renderer passes{resources};
    pass_description description;
    description.color.target = resources.create_texture({8, 8, texture_role::color_target});
    const std::array plan{description};
    gpu_gate gate{graphics};
    const auto done = passes.submit(frame_plan{plan});
    passes.collect();
    EXPECT_EQ(passes.gpu_timing().completed_frames, 0U);
    gate.open();
    resources.wait(done);
    passes.collect();
    EXPECT_EQ(passes.gpu_timing().completed_frames, diagnostics::instrumentation_enabled ? 1U : 0U);
    EXPECT_GE(passes.gpu_timing().last_duration_us, 0);
    std::ostringstream trace;
    capture->write_chrome_trace(trace);
    EXPECT_EQ(trace.str().find("renderer.gpu.duration_us") != std::string::npos,
              diagnostics::instrumentation_enabled);
    EXPECT_EQ(capture->statistics().errors, 0U);
    EXPECT_EQ(graphics.debug_error_count(), 0U);
}
} // namespace atlas::renderer::tests
