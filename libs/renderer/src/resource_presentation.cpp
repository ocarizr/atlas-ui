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
    if (!entry.initialized || entry.description.role == texture_role::depth_target)
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
