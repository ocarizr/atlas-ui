/** @file @brief Swapchain creation, resize and retained lifetime management. */
#include "native_renderer.hpp"

namespace atlas::renderer::detail
{
/// @brief Throw an operation-specific diagnostic before continuing after a native failure.
native_surface::native_surface(std::shared_ptr<native_device> graphics,
                               std::shared_ptr<window::detail::window_backend> window,
                               surface_options policy)
    : context(std::move(graphics)), target(std::move(window)), options(policy),
      frames(context->memory.get())
{
    const auto state = target->state();
    if (state.close_requested || state.width == 0 || state.height == 0)
    {
        throw std::invalid_argument("presentation requires an open, nonzero window");
    }

    extent = {state.width, state.height};
    backing_extent = extent;
    DXGI_SWAP_CHAIN_DESC1 description{};
    description.Width = extent.width;
    description.Height = extent.height;
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.BufferCount = options.frame_count;
    description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    description.Scaling = DXGI_SCALING_STRETCH;
    com_ptr<IDXGISwapChain1> created;
    const auto handle = static_cast<HWND>(target->native_handle());
    check(context->factory->CreateSwapChainForHwnd(context->queue.Get(), handle, &description,
                                                   nullptr, nullptr, &created),
          "create presentation swapchain");
    check(created.As(&swapchain), "query swapchain");
    check(context->factory->MakeWindowAssociation(handle, DXGI_MWA_NO_ALT_ENTER),
          "disable implicit fullscreen");
    D3D12_DESCRIPTOR_HEAP_DESC heap{};
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heap.NumDescriptors = options.frame_count;
    check(context->gpu->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&views)), "create RTV heap");
    descriptor_size =
        context->gpu->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    frames.resize(options.frame_count);
    for (auto& slot : frames)
    {
        check(context->gpu->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                   IID_PPV_ARGS(&slot.allocator)),
              "create frame allocator");
    }

    create_views();
    check(context->gpu->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                          frames[0].allocator.Get(), nullptr,
                                          IID_PPV_ARGS(&commands)),
          "create graphics command list");
    check(commands->Close(), "close initial command list");
}

/// @brief Release retained resources on the owner thread after outstanding work completes.
native_surface::~native_surface()
{
    context->drain_noexcept();
}

/// @brief Calculate a CPU descriptor handle using the native descriptor stride.
D3D12_CPU_DESCRIPTOR_HANDLE native_surface::rtv(std::uint32_t index) const
{
    auto handle = views->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(index) * descriptor_size;
    return handle;
}

/// @brief Throw an operation-specific diagnostic before continuing after a native failure.
void native_surface::create_views()
{
    for (std::uint32_t index = 0; index < options.frame_count; ++index)
    {
        check(swapchain->GetBuffer(index, IID_PPV_ARGS(&frames[index].buffer)),
              "retrieve swapchain buffer");
        context->gpu->CreateRenderTargetView(frames[index].buffer.Get(), nullptr, rtv(index));
    }
}

/// @brief Reject access outside the single graphics coordinator thread.
void native_surface::resize(surface_extent requested)
{
    context->check_owner();
    if (requested.width == 0 || requested.height == 0)
    {
        extent = requested;
        has_frame = false;
        return;
    }

    if (requested.width == backing_extent.width && requested.height == backing_extent.height)
    {
        extent = requested;
        return;
    }

    context->wait_idle();
    has_frame = false;
    extent = {};
    backing_extent = {};
    for (auto& slot : frames)
    {
        slot.buffer.Reset();
    }

    check(swapchain->ResizeBuffers(options.frame_count, requested.width, requested.height,
                                   DXGI_FORMAT_R8G8B8A8_UNORM, 0),
          "resize presentation buffers");
    create_views();
    backing_extent = requested;
    extent = requested;
}

// The directional state pair follows D3D12; every caller supplies explicit SDK state constants.
/// @brief Record a whole-resource state transition in the exclusive command list.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void native_surface::transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
                                D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    commands->ResourceBarrier(1, &barrier);
}

/// @brief Reject access outside the single graphics coordinator thread.
frame_result native_surface::render(const frame_description& frame)
{
    context->check_owner();
    if (target->state().close_requested)
    {
        return frame_result::surface_closed;
    }

    if (extent.width == 0 || extent.height == 0 || target->state().minimized)
    {
        return frame_result::suspended;
    }

    const auto index = swapchain->GetCurrentBackBufferIndex();
    auto& slot = frames[index];
    context->wait(slot.fence);
    check(slot.allocator->Reset(), "reset completed frame allocator");
    check(commands->Reset(slot.allocator.Get(), context->pipeline.Get()), "begin frame commands");
    transition(slot.buffer.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    const auto output = rtv(index);
    commands->OMSetRenderTargets(1, &output, FALSE, nullptr);
    commands->ClearRenderTargetView(output, frame.clear_color.data(), 0, nullptr);
    if (frame.draw_triangle)
    {
        const D3D12_VIEWPORT viewport{
            0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height), 0, 1};
        const D3D12_RECT scissor{0, 0, static_cast<LONG>(extent.width),
                                 static_cast<LONG>(extent.height)};
        commands->RSSetViewports(1, &viewport);
        commands->RSSetScissorRects(1, &scissor);
        commands->SetGraphicsRootSignature(context->root.Get());
        commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commands->DrawInstanced(3, 1, 0, 0);
    }

    transition(slot.buffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    check(commands->Close(), "finish frame commands");
    ID3D12CommandList* lists[]{commands.Get()};
    context->queue->ExecuteCommandLists(1, lists);
    slot.fence = context->signal();
    last_index = index;
    has_frame = true;
    ++frame_count;
    const auto result = swapchain->Present(options.vsync ? 1U : 0U, 0);
    check(result, "present frame");
    return result == DXGI_STATUS_OCCLUDED ? frame_result::occluded : frame_result::presented;
}
} // namespace atlas::renderer::detail
