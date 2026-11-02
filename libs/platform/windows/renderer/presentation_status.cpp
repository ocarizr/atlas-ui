/** @file @brief Native presentation status boundary and non-rendering visibility probes. */
#include "native_renderer.hpp"

namespace atlas::renderer::detail
{
/// @brief Keep normal presentation and DXGI_PRESENT_TEST calls behind the same native boundary.
HRESULT native_surface::present_native(bool probe)
{
    return swapchain->Present(probe ? 0U : (options.vsync ? 1U : 0U),
                              probe ? DXGI_PRESENT_TEST : 0U);
}

/// @brief Only query visibility after a native occlusion result.
bool native_surface::resume_if_visible()
{
    if (!occluded)
    {
        return true;
    }

    const auto result = present_native(true);
    context->check_result(result, "probe presentation visibility");
    occluded = result == DXGI_STATUS_OCCLUDED;
    return !occluded;
}
} // namespace atlas::renderer::detail
