/** @file @brief Explicit fail-stop device loss; resource reconstruction belongs to composition. */
#include "native_renderer.hpp"

namespace atlas::renderer::detail
{
/// @brief Preserve HRESULT bits while translating common terminal failure classes.
device_status native_device::health() const
{
    const auto reason = gpu->GetDeviceRemovedReason();
    if (SUCCEEDED(reason))
    {
        return {};
    }

    device_condition condition = device_condition::driver_error;
    if (reason == DXGI_ERROR_DEVICE_REMOVED)
    {
        condition = device_condition::removed;
    }
    else if (reason == DXGI_ERROR_DEVICE_RESET)
    {
        condition = device_condition::reset;
    }
    else if (reason == DXGI_ERROR_DEVICE_HUNG)
    {
        condition = device_condition::hung;
    }

    return {condition, static_cast<std::uint32_t>(reason)};
}

/// @brief One health query at an operation boundary, never per draw or per metadata lookup.
void native_device::ensure_healthy(const char* operation) const
{
    check_owner();
    const auto current = health();
    if (current.lost())
    {
        throw device_lost{current, operation};
    }
}

/// @brief Report a terminal device reason when a native call fails after its initial health check.
void native_device::check_result(HRESULT result, const char* operation) const
{
    if (FAILED(result))
    {
        ensure_healthy(operation);
    }

    check(result, operation);
}
} // namespace atlas::renderer::detail
