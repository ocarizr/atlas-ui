/** @file @brief Copy generic pass output to a swapchain without CPU readback. */
#include "native_resources.hpp"

namespace atlas::renderer::detail
{
/// @brief Copy a same-device, same-extent RGBA source while preserving its scheduled state.
surface_copy_result native_surface::present(const std::shared_ptr<gpu_resource>& source,
                                            resource_state state, std::shared_ptr<void> owner,
                                            std::uint64_t device_identity)
{
    context->ensure_healthy("present rendered texture");
    if (context->identity != device_identity)
    {
        throw std::invalid_argument("surface and source belong to different devices");
    }

    if (target->state().close_requested)
    {
        return {frame_result::surface_closed};
    }

    if (extent.width == 0 || extent.height == 0 || target->state().minimized)
    {
        return {frame_result::suspended};
    }

    const auto native = std::static_pointer_cast<native_resource>(source);
    if (native->description.Width != extent.width || native->description.Height != extent.height ||
        native->description.Format != DXGI_FORMAT_R8G8B8A8_UNORM)
    {
        throw std::invalid_argument("presentation source must match the surface RGBA extent");
    }

    const auto index = swapchain->GetCurrentBackBufferIndex();
    if (!resume_if_visible())
    {
        return {frame_result::occluded, 0};
    }

    auto& slot = frames[index];
    context->wait(slot.fence);
    slot.source_owner.reset();
    check(slot.allocator->Reset(), "reset copy frame allocator");
    check(commands->Reset(slot.allocator.Get(), nullptr), "begin surface copy");
    const auto before = native_state(state, true);
    barrier(*commands.Get(), native->get(), before, D3D12_RESOURCE_STATE_COPY_SOURCE);
    barrier(*commands.Get(), slot.buffer.Get(), D3D12_RESOURCE_STATE_PRESENT,
            D3D12_RESOURCE_STATE_COPY_DEST);
    commands->CopyResource(slot.buffer.Get(), native->get());
    barrier(*commands.Get(), native->get(), D3D12_RESOURCE_STATE_COPY_SOURCE, before);
    barrier(*commands.Get(), slot.buffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_PRESENT);
    check(commands->Close(), "finish surface copy");
    ID3D12CommandList* lists[]{commands.Get()};
    context->queue->ExecuteCommandLists(1, lists);
    try
    {
        slot.fence = context->signal();
    }
    catch (...)
    {
        context->drain_noexcept();
        throw;
    }

    slot.source_owner = std::move(owner);
    last_index = index;
    has_frame = true;
    ++frame_count;
    const auto result = present_native(false);
    // A failed Present must not let the caller release a source still used by the queue.
    if (FAILED(result))
    {
        context->drain_noexcept();
        context->check_result(result, "present copied frame");
    }

    occluded = result == DXGI_STATUS_OCCLUDED;
    return {occluded ? frame_result::occluded : frame_result::presented, slot.fence};
}
} // namespace atlas::renderer::detail
