/** @file @brief Private platform adapter for graphics ownership. */
#pragma once
#include "window_backend.hpp"
#include <atlas/renderer/renderer.hpp>
#include <thread>

namespace atlas::renderer::detail
{
/// @brief Shared native queue implementation and owner guard.
class device_backend
{
  public:
    /// @brief Release retained resources on the owner thread after outstanding work completes.
    virtual ~device_backend() = default;

    /// @brief Reject access outside the single graphics coordinator thread.
    void check_owner() const
    {
        if (owner != std::this_thread::get_id())
        {
            throw std::logic_error("renderer requires its owner thread");
        }
    }

    virtual device_info info() const = 0;               ///< Return adapter capabilities.
    virtual void wait_idle() = 0;                       ///< Complete queued GPU work.
    virtual std::uint64_t errors() const = 0;           ///< Native validation errors.
    std::thread::id owner = std::this_thread::get_id(); ///< Exclusive native coordinator.
};

/// @brief Swapchain implementation with a retained queue and native window.
class surface_backend
{
  public:
    /// @brief Release retained resources on the owner thread after outstanding work completes.
    virtual ~surface_backend() = default;
    virtual void resize(surface_extent extent) = 0;                  ///< Resize or suspend.
    virtual frame_result render(const frame_description& frame) = 0; ///< Record and present.
    virtual frame_image readback() = 0;          ///< Copy the previous backbuffer to CPU memory.
    virtual std::uint64_t submitted() const = 0; ///< Submitted frame count.
};

std::shared_ptr<device_backend>
make_device(device_options options,
            std::shared_ptr<std::pmr::memory_resource> memory); ///< Select backend.
std::unique_ptr<surface_backend>
make_surface(std::shared_ptr<device_backend> device,
             std::shared_ptr<window::detail::window_backend> target,
             surface_options options); ///< Create retained surface.
} // namespace atlas::renderer::detail
