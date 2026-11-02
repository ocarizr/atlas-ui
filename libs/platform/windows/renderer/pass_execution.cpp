/** @file @brief Ordered D3D12 passes and fence-protected reusable command batches. */
#include "native_passes.hpp"

namespace atlas::renderer::detail
{
namespace
{
/// @brief Resolve internal GPU ownership only within the platform adapter.
ID3D12Resource* resource(const pass_resource& value)
{
    return std::static_pointer_cast<native_resource>(value.allocation)->get();
}

/// @brief Transition the resolved input from its validated scheduled state.
void transition(ID3D12GraphicsCommandList& commands, const pass_resource& value,
                resource_state target, bool texture)
{
    barrier(commands, resource(value), native_state(value.before, texture),
            native_state(target, texture));
}
} // namespace

/// @brief Reserve command ownership before any ExecuteCommandLists call.
native_passes::native_passes(std::shared_ptr<native_resources> store, pass_limits budgets)
    : resources(std::move(store)), limits(budgets), pending(resources->memory()),
      reusable(resources->memory())
{
    pending.reserve(limits.max_frames);
    reusable.reserve(limits.max_frames);
}

/// @brief Move completed command storage into the reusable pool without waiting.
void native_passes::collect()
{
    const auto done = resources->completed();
    for (auto iterator = pending.begin(); iterator != pending.end();)
    {
        if (iterator->fence > done)
        {
            ++iterator;
            continue;
        }

        reusable.push_back(std::move(*iterator));
        iterator = pending.erase(iterator);
    }
}

/// @brief Reuse completed allocators or create within the fixed frame capacity.
pass_batch native_passes::begin()
{
    collect();
    if (pending.size() >= limits.max_frames)
    {
        throw std::overflow_error("render frame slots exhausted; collect completed work");
    }

    pass_batch batch;
    if (!reusable.empty())
    {
        batch = std::move(reusable.back());
        reusable.pop_back();
        check(batch.allocator->Reset(), "reset pass allocator");
        check(batch.commands->Reset(batch.allocator.Get(), nullptr), "reset pass command list");
    }
    else
    {
        auto* gpu = resources->context->gpu.Get();
        check(gpu->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                          IID_PPV_ARGS(&batch.allocator)),
              "create pass allocator");
        check(gpu->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, batch.allocator.Get(),
                                     nullptr, IID_PPV_ARGS(&batch.commands)),
              "create pass command list");
        D3D12_DESCRIPTOR_HEAP_DESC heap{};
        heap.NumDescriptors = limits.max_passes;
        heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        check(gpu->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&batch.colors)),
              "create pass RTV heap");
        heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        check(gpu->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&batch.depths)),
              "create pass DSV heap");
    }

    return batch;
}

/// @brief Record explicit attachment operations followed by stable-order triangle draws.
void native_passes::record(pass_batch& batch, const resolved_pass& pass, std::uint32_t index)
{
    auto& commands = *batch.commands.Get();
    auto* gpu = resources->context->gpu.Get();
    auto color = batch.colors->GetCPUDescriptorHandleForHeapStart();
    color.ptr += static_cast<SIZE_T>(index) *
                 gpu->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    transition(commands, pass.color, resource_state::render_target, true);
    gpu->CreateRenderTargetView(resource(pass.color), nullptr, color);
    auto depth = batch.depths->GetCPUDescriptorHandleForHeapStart();
    if (pass.depth)
    {
        if (!pass.description.depth)
        {
            throw std::logic_error("resolved depth attachment is missing its operations");
        }

        depth.ptr += static_cast<SIZE_T>(index) *
                     gpu->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
        transition(commands, *pass.depth, resource_state::depth_write, true);
        gpu->CreateDepthStencilView(resource(*pass.depth), nullptr, depth);
        if (pass.description.depth->load == load_action::clear)
        {
            commands.ClearDepthStencilView(depth, D3D12_CLEAR_FLAG_DEPTH,
                                           pass.description.depth->clear_depth, 0, 0, nullptr);
        }
    }

    commands.OMSetRenderTargets(1, &color, FALSE, pass.depth ? &depth : nullptr);
    if (pass.description.color.load == load_action::clear)
    {
        commands.ClearRenderTargetView(color, pass.description.color.clear_color.data(), 0,
                                       nullptr);
    }

    commands.IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    for (const auto& draw : pass.draws)
    {
        const auto pipeline = std::static_pointer_cast<native_pipeline>(draw.pipeline);
        transition(commands, draw.vertices, resource_state::shader_read, false);
        commands.SetPipelineState(pipeline->state.Get());
        commands.SetGraphicsRootSignature(pipeline->root.Get());
        commands.SetGraphicsRoot32BitConstants(0, 16, draw.packet.transform.data(), 0);
        commands.SetGraphicsRoot32BitConstants(0, 4, draw.packet.tint.data(), 16);
        if (draw.texture)
        {
            transition(commands, *draw.texture, resource_state::shader_read, true);
            auto handle = resources->descriptors->GetGPUDescriptorHandleForHeapStart();
            handle.ptr +=
                static_cast<UINT64>(draw.packet.texture.index) *
                gpu->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            commands.SetGraphicsRootDescriptorTable(1, handle);
        }

        const auto viewport = draw.packet.viewport.value_or(pixel_viewport{
            0, 0, static_cast<float>(pass.extent.width), static_cast<float>(pass.extent.height)});
        const D3D12_VIEWPORT native_viewport{viewport.x,      viewport.y, viewport.width,
                                             viewport.height, 0,          1};
        commands.RSSetViewports(1, &native_viewport);
        const auto scissor = draw.packet.scissor.value_or(
            pixel_rectangle{0, 0, pass.extent.width, pass.extent.height});
        const D3D12_RECT native_scissor{
            static_cast<LONG>(scissor.left), static_cast<LONG>(scissor.top),
            static_cast<LONG>(scissor.right), static_cast<LONG>(scissor.bottom)};
        commands.RSSetScissorRects(1, &native_scissor);
        auto* vertices = resource(draw.vertices);
        const D3D12_VERTEX_BUFFER_VIEW input{vertices->GetGPUVirtualAddress(),
                                             static_cast<UINT>(vertices->GetDesc().Width),
                                             sizeof(vertex)};
        commands.IASetVertexBuffers(0, 1, &input);
        commands.DrawInstanced(draw.packet.vertex_count, 1, draw.packet.first_vertex, 0);
    }

    if (pass.description.color.store == store_action::discard)
    {
        commands.DiscardResource(resource(pass.color), nullptr);
    }

    if (pass.depth && pass.description.depth &&
        pass.description.depth->store == store_action::discard)
    {
        commands.DiscardResource(resource(*pass.depth), nullptr);
    }
}

/// @brief Submit after recording succeeds; retain command storage without post-execute allocation.
std::uint64_t native_passes::submit(std::span<const resolved_pass> passes)
{
    auto batch = begin();
    ID3D12DescriptorHeap* heaps[]{resources->descriptors.Get()};
    batch.commands->SetDescriptorHeaps(1, heaps);
    for (std::size_t index = 0; index < passes.size(); ++index)
    {
        record(batch, passes[index], static_cast<std::uint32_t>(index));
    }

    check(batch.commands->Close(), "finish pass command list");
    ID3D12CommandList* lists[]{batch.commands.Get()};
    resources->context->queue->ExecuteCommandLists(1, lists);
    try
    {
        batch.fence = resources->context->signal();
    }
    catch (...)
    {
        resources->drain();
        throw;
    }

    const auto fence = batch.fence;
    pending.push_back(std::move(batch));
    return fence;
}

/// @brief Create the selected native pass adapter without another device or queue.
std::unique_ptr<pass_backend> make_pass_backend(const std::shared_ptr<resource_backend>& resources,
                                                pass_limits limits)
{
    return std::make_unique<native_passes>(std::static_pointer_cast<native_resources>(resources),
                                           limits);
}
} // namespace atlas::renderer::detail
