/** @file @brief Private platform adapter for graphics ownership. */
#pragma once
#include "window_backend.hpp"
#include <atlas/renderer/renderer.hpp>
#include <thread>

namespace atlas::renderer
{
/// @brief Scheduled state declaration for SDK-free resource presentation.
enum class resource_state : std::uint8_t;
} // namespace atlas::renderer

namespace atlas::renderer::detail
{
/// @brief Opaque resource declaration for presentation copies.
class gpu_resource;

/// @brief Presentation may suspend without submitting GPU work.
struct surface_copy_result
{
    frame_result result;     ///< Native presentation status.
    std::uint64_t fence = 0; ///< Zero when no commands were submitted.
};

std::uint64_t next_identity(); ///< Allocate a nonzero process-local provenance ID.

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
    std::uint64_t identity = next_identity(); ///< Fence provenance shared by device consumers.
};

/// @brief Private SDK-free bridge; native callers downcast only within the platform boundary.
class device_access
{
  public:
    static std::shared_ptr<device_backend>
    retain(device& value); ///< Check coordinator before retaining its backend.
};

/// @brief Swapchain implementation with a retained queue and native window.
class surface_backend
{
  public:
    /// @brief Release retained resources on the owner thread after outstanding work completes.
    virtual ~surface_backend() = default;
    virtual void resize(surface_extent extent) = 0;                  ///< Resize or suspend.
    virtual frame_result render(const frame_description& frame) = 0; ///< Record and present.
    virtual surface_copy_result
    present(const std::shared_ptr<gpu_resource>& source, resource_state state,
            std::shared_ptr<void> owner,
            std::uint64_t device_identity) = 0;  ///< Copy a retained resource.
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
