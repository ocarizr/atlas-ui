/** @file @brief Resource-to-surface presentation with fence-protected source ownership. */
#include "resource_store_state.hpp"

namespace atlas::renderer
{
/// @brief Present initialized color data and extend its lifetime through the surface submission.
frame_result presentation_surface::present(resource_store& resources, texture_handle source)
{
    auto& entry = resources.impl_->get(source);
    const diagnostics::operation_scope scope{diagnostics_,
                                             {"surface.present_source", "renderer.cpu"}};
    const auto format = entry.description.resolved_format();
    if (!entry.initialized || entry.description.mip_levels != 1 ||
        (format != texture_format::rgba8_unorm && format != texture_format::rgba8_srgb))
    {
        throw std::invalid_argument("presentation requires initialized RGBA contents");
    }

    const auto result = backend_->present(entry.allocation, entry.state, resources.impl_,
                                          resources.impl_->device->identity);
    if (result.fence != 0)
    {
        entry.last_use = result.fence;
    }

    return result.result;
}
} // namespace atlas::renderer
