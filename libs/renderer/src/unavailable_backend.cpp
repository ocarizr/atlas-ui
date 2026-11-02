/** @file @brief SDK-free builds report graphics as unavailable. */
#include "renderer_backend.hpp"

namespace atlas::renderer
{
/// @brief Report native backend availability without allocating graphics resources.
bool backend_available() noexcept
{
    return false;
}

/// @brief Construct the selected backend while retaining the CPU allocation resource.
std::shared_ptr<detail::device_backend>
detail::make_device(device_options, std::shared_ptr<std::pmr::memory_resource>)
{
    throw std::runtime_error("D3D12 backend is unavailable in this build");
}

/// @brief Retain the device and native window when creating a presentation surface.
std::unique_ptr<detail::surface_backend>
detail::make_surface(std::shared_ptr<device_backend>,
                     std::shared_ptr<window::detail::window_backend>, surface_options)
{
    throw std::runtime_error("D3D12 backend is unavailable in this build");
}
} // namespace atlas::renderer
