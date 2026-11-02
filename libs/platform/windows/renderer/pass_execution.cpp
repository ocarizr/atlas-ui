/** @file @brief Ordered D3D12 passes and fence-protected reusable command batches. */
#include "native_passes.hpp"

namespace atlas::renderer::detail
{
namespace
{
/// @brief Resolve internal GPU ownership only within the platform adapter.
ID3D12Resource* resource(const pass_resource& value)
{
    return static_cast<native_resource*>(value.allocation.get())->get();
}

/// @brief Transition the resolved input from its validated scheduled state.
void transition(ID3D12GraphicsCommandList& commands, const pass_resource& value,
                resource_state target, bool texture)
{
    barrier(commands, resource(value), native_state(value.before, texture),
            native_state(target, texture));
}
} // namespace

/// @brief Record explicit attachment operations followed by stable-order triangle draws.
void native_passes::record(pass_batch& batch, const resolved_pass& pass,
                           const recording_section& section)
{
    auto& commands = *batch.commands.Get();
    auto* gpu = resources->context->gpu.Get();
    auto color = batch.colors->GetCPUDescriptorHandleForHeapStart();
    color.ptr += static_cast<SIZE_T>(section.pass) *
                 gpu->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    if (section.first)
    {
        transition(commands, pass.color, resource_state::render_target, true);
    }

    gpu->CreateRenderTargetView(resource(pass.color), nullptr, color);
    auto depth = batch.depths->GetCPUDescriptorHandleForHeapStart();
    if (pass.depth)
    {
        if (!pass.description.depth)
        {
            throw std::logic_error("resolved depth attachment is missing its operations");
        }

        depth.ptr += static_cast<SIZE_T>(section.pass) *
                     gpu->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
        if (section.first)
        {
            transition(commands, *pass.depth, resource_state::depth_write, true);
        }

        gpu->CreateDepthStencilView(resource(*pass.depth), nullptr, depth);
        if (section.first && pass.description.depth->load == load_action::clear)
        {
            commands.ClearDepthStencilView(depth, D3D12_CLEAR_FLAG_DEPTH,
                                           pass.description.depth->clear_depth, 0, 0, nullptr);
        }
    }

    commands.OMSetRenderTargets(1, &color, FALSE, pass.depth ? &depth : nullptr);
    if (section.first && pass.description.color.load == load_action::clear)
    {
        commands.ClearRenderTargetView(color, pass.description.color.clear_color.data(), 0,
                                       nullptr);
    }

    commands.IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    for (const auto& draw : std::span{pass.draws}.subspan(section.begin, section.count))
    {
        const auto* pipeline = static_cast<const native_pipeline*>(draw.pipeline.get());
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

    if (section.last && pass.description.color.store == store_action::discard)
    {
        commands.DiscardResource(resource(pass.color), nullptr);
    }

    if (section.last && pass.depth && pass.description.depth &&
        pass.description.depth->store == store_action::discard)
    {
        commands.DiscardResource(resource(*pass.depth), nullptr);
    }
}

} // namespace atlas::renderer::detail
