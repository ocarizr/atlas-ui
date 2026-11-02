/** @file @brief SDK-free builds report graphics as unavailable. */
#include "pass_backend.hpp"
#include "renderer_backend.hpp"
#include "resource_backend.hpp"

namespace atlas::renderer
{
/// @brief Reject native pass creation when no graphics implementation is selected.
std::unique_ptr<detail::pass_backend>
detail::make_pass_backend(const std::shared_ptr<resource_backend>&, pass_limits)
{
    throw std::runtime_error("GPU pass backend is unavailable in this build");
}

/// @brief Resource operations cannot silently use a CPU substitute for a native GPU backend.
std::shared_ptr<detail::resource_backend>
detail::make_resource_backend(std::shared_ptr<device_backend>, resource_limits)
{
    throw std::runtime_error("GPU resource backend is unavailable in this build");
}

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
