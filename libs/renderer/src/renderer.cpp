/** @file @brief Generic renderer contracts and ownership. */
#include "renderer_backend.hpp"
#include <atlas/renderer/renderer.hpp>
#include <atomic>
#include <cmath>
#include <limits>

namespace atlas::renderer
{
/// @brief Allocate provenance without wrapping back into earlier live handle identities.
std::uint64_t detail::next_identity()
{
    static std::atomic<std::uint64_t> next{1};
    auto value = next.load();
    do
    {
        if (value == std::numeric_limits<std::uint64_t>::max())
        {
            throw std::overflow_error("renderer identity space exhausted");
        }
    } while (!next.compare_exchange_weak(value, value + 1));

    return value;
}

/// @brief Retain only on the native coordinator.
std::shared_ptr<detail::device_backend> detail::device_access::retain(device& value)
{
    value.backend_->check_owner();
    return value.backend_;
}

/// @brief Reject invalid configuration before allocating or mutating native resources.
void device_options::validate() const
{
    if (gpu_wait_timeout_ms == 0 || gpu_wait_timeout_ms == 0xffffffffU ||
        (adapter != adapter_preference::prefer_hardware &&
         adapter != adapter_preference::software) ||
        (debug != debug_validation::disabled && debug != debug_validation::when_available &&
         debug != debug_validation::required))
    {
        throw std::invalid_argument("invalid device policy or GPU wait timeout");
    }
}

/// @brief Reject invalid configuration before allocating or mutating native resources.
void surface_options::validate() const
{
    if (frame_count < 2 || frame_count > 3)
    {
        throw std::invalid_argument("frame_count must be two or three");
    }
}

/// @brief Reject invalid configuration before allocating or mutating native resources.
void surface_extent::validate() const
{
    if (width > 16384 || height > 16384)
    {
        throw std::invalid_argument("surface extent exceeds 16384 pixels");
    }
}

/// @brief Reject invalid configuration before allocating or mutating native resources.
void frame_description::validate() const
{
    for (const auto value : clear_color)
    {
        if (!std::isfinite(value) || value < 0 || value > 1)
        {
            throw std::invalid_argument("clear color must be finite normalized RGBA");
        }
    }
}

/// @brief Construct the selected backend while retaining the CPU allocation resource.
device::device(device_options options, std::shared_ptr<std::pmr::memory_resource> memory,
               diagnostics::services destinations)
    : diagnostics_(std::move(destinations))
{
    const diagnostics::operation_scope scope{diagnostics_, {"device.initialize", "renderer.cpu"}};
    options.validate();
    backend_ = detail::make_device(options, std::move(memory));
    diagnostics::write(diagnostics_, diagnostics::log_level::info, "renderer",
                       "Graphics device initialized");
}

/// @brief Release retained resources on the owner thread after outstanding work completes.
device::~device()
{
    const diagnostics::operation_scope scope{diagnostics_, {"device.release", "renderer.cpu"}};
    backend_.reset();
    diagnostics::write(diagnostics_, diagnostics::log_level::info, "renderer",
                       "Graphics device facade released");
}

/// @brief Reject access outside the single graphics coordinator thread.
device_info device::info() const
{
    backend_->check_owner();
    return backend_->info();
}

/// @brief Reject access outside the single graphics coordinator thread.
void device::wait_idle()
{
    const diagnostics::operation_scope scope{diagnostics_,
                                             {"device.wait_idle", "renderer.cpu_wait"}};
    backend_->check_owner();
    backend_->wait_idle();
}

/// @brief Reject access outside the single graphics coordinator thread.
std::uint64_t device::debug_error_count() const
{
    backend_->check_owner();
    return backend_->errors();
}

/// @brief Reject access outside the single graphics coordinator thread.
presentation_surface::presentation_surface(device& graphics, window::window& target,
                                           surface_options options)
    : diagnostics_(graphics.diagnostics_)
{
    const diagnostics::operation_scope scope{diagnostics_, {"surface.initialize", "renderer.cpu"}};
    options.validate();
    graphics.backend_->check_owner();
    backend_ = detail::make_surface(graphics.backend_,
                                    window::detail::window_access::retain(target), options);
}

/// @brief Release retained resources on the owner thread after outstanding work completes.
presentation_surface::presentation_surface(device& graphics,
                                           const window::presentation_target& target,
                                           surface_options options)
    : diagnostics_(graphics.diagnostics_)
{
    const diagnostics::operation_scope scope{diagnostics_, {"surface.initialize", "renderer.cpu"}};
    options.validate();
    graphics.backend_->check_owner();
    backend_ = detail::make_surface(graphics.backend_,
                                    window::detail::window_access::retain(target), options);
}

/// @brief Release retained resources on the owner thread after outstanding work completes.
presentation_surface::~presentation_surface()
{
    const diagnostics::operation_scope scope{diagnostics_,
                                             {"surface.shutdown", "renderer.cpu_wait"}};
    backend_.reset();
    diagnostics::write(diagnostics_, diagnostics::log_level::info, "renderer",
                       "Presentation surface drained and released");
}

/// @brief Update physical extents, suspending zero-size rendering and draining before buffer
/// replacement.
void presentation_surface::resize(surface_extent extent)
{
    const diagnostics::operation_scope scope{diagnostics_, {"surface.resize", "renderer.cpu"}};
    extent.validate();
    backend_->resize(extent);
}

/// @brief Validate frame data before submitting any GPU commands.
frame_result presentation_surface::render_frame(const frame_description& frame)
{
    const diagnostics::operation_scope scope{diagnostics_, {"surface.frame", "renderer.cpu"}};
    frame.validate();
    return backend_->render(frame);
}

/// @brief Synchronize and return tightly packed CPU bytes from the last rendered frame.
frame_image presentation_surface::readback_last_frame()
{
    const diagnostics::operation_scope scope{diagnostics_,
                                             {"surface.readback", "renderer.cpu_wait"}};
    return backend_->readback();
}

/// @brief Read the count of executed graphics frames on the owner thread.
std::uint64_t presentation_surface::submitted_frames() const
{
    return backend_->submitted();
}
} // namespace atlas::renderer
