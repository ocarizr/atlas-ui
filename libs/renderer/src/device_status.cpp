/** @file @brief Typed terminal loss and SDK-free device health queries. */
#include "renderer_backend.hpp"

namespace atlas::renderer
{
/// @brief Retain diagnostic bits independently of the native device object's lifetime.
device_lost::device_lost(device_status status, const char* operation)
    : std::runtime_error(std::string{operation} + ": device lost (native code " +
                         std::to_string(status.native_code) + ")"),
      status_(status)
{
}

/// @brief Health remains observable after loss without silently recreating device resources.
device_status device::status() const
{
    backend_->check_owner();
    return backend_->health();
}
} // namespace atlas::renderer
