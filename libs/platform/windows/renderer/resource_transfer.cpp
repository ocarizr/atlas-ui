/** @file @brief Bounded full-resource uploads and explicit single-queue state transitions. */
#include "native_resources.hpp"
#include <cstring>

namespace atlas::renderer::detail
{
/// @brief Translate supported platform-neutral states to the selected D3D12 resource kind.
D3D12_RESOURCE_STATES native_state(resource_state state, bool texture)
{
    switch (state)
    {
        case resource_state::copy_destination:
            return D3D12_RESOURCE_STATE_COPY_DEST;
        case resource_state::copy_source:
            return D3D12_RESOURCE_STATE_COPY_SOURCE;
        case resource_state::render_target:
            return D3D12_RESOURCE_STATE_RENDER_TARGET;
        case resource_state::depth_write:
            return D3D12_RESOURCE_STATE_DEPTH_WRITE;
        case resource_state::shader_read:
            return texture ? D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                                 D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
                           : D3D12_RESOURCE_STATE_GENERIC_READ;
    }

    throw std::invalid_argument("unsupported GPU resource state");
}

/// @brief Record only nonredundant whole-resource state transitions.
void barrier(ID3D12GraphicsCommandList& commands, ID3D12Resource* resource,
             D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    if (before == after)
    {
        return;
    }

    D3D12_RESOURCE_BARRIER value{};
    value.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    value.Transition.pResource = resource;
    value.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    value.Transition.StateBefore = before;
    value.Transition.StateAfter = after;
    commands.ResourceBarrier(1, &value);
}

/// @brief Reuse completed command objects or lazily create within the bounded transfer capacity.
transfer_batch native_resources::begin(const std::shared_ptr<gpu_resource>& destination)
{
    collect(completed());
    if (pending.size() >= limits.max_transfers)
    {
        throw std::overflow_error("GPU transfer slots exhausted");
    }

    transfer_batch batch;
    if (!reusable.empty())
    {
        batch = std::move(reusable.back());
        reusable.pop_back();
        check(batch.allocator->Reset(), "reset completed transfer allocator");
        check(batch.commands->Reset(batch.allocator.Get(), nullptr), "reset transfer commands");
    }
    else
    {
        check(context->gpu->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                   IID_PPV_ARGS(&batch.allocator)),
              "create transfer allocator");
        check(context->gpu->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                              batch.allocator.Get(), nullptr,
                                              IID_PPV_ARGS(&batch.commands)),
              "create transfer commands");
    }

    batch.destination = destination;
    return batch;
}

/// @brief Execute and retain command resources after all potentially allocating admission steps.
std::uint64_t native_resources::submit(transfer_batch batch)
{
    check(batch.commands->Close(), "finish resource commands");
    ID3D12CommandList* lists[]{batch.commands.Get()};
    context->queue->ExecuteCommandLists(1, lists);
    try
    {
        batch.fence = context->signal();
    }
    catch (...)
    {
        context->drain_noexcept();
        throw;
    }

    const auto fence = batch.fence;
    staging_bytes += batch.bytes;
    pending.push_back(std::move(batch));
    return fence;
}

/// @brief Copy exact source bytes into retained staging and publish the protecting queue fence.
std::uint64_t native_resources::upload(const std::shared_ptr<gpu_resource>& resource,
                                       resource_state before, std::span<const std::byte> bytes)
{
    auto batch = begin(resource);
    auto native = std::static_pointer_cast<native_resource>(resource);
    const bool texture = native->description.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 size = bytes.size();
    if (texture)
    {
        context->gpu->GetCopyableFootprints(&native->description, 0, 1, 0, &footprint, nullptr,
                                            nullptr, &size);
    }

    if (size > limits.max_staging_bytes - staging_bytes)
    {
        throw std::length_error("GPU upload staging budget exhausted");
    }

    batch.staging =
        allocate(buffer_desc(size), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    batch.bytes = size;
    auto* upload = batch.staging->GetResource();
    void* mapped = nullptr;
    const D3D12_RANGE empty{0, 0};
    check(upload->Map(0, &empty, &mapped), "map upload staging");
    if (texture)
    {
        const auto row_bytes = static_cast<std::size_t>(native->description.Width) * 4;
        for (std::uint32_t row = 0; row < native->description.Height; ++row)
        {
            std::memcpy(static_cast<std::byte*>(mapped) +
                            static_cast<std::size_t>(row) * footprint.Footprint.RowPitch,
                        bytes.data() + static_cast<std::size_t>(row) * row_bytes, row_bytes);
        }
    }
    else
    {
        std::memcpy(mapped, bytes.data(), bytes.size());
    }

    const D3D12_RANGE written{0, static_cast<SIZE_T>(size)};
    upload->Unmap(0, &written);
    barrier(*batch.commands.Get(), native->get(), native_state(before, texture),
            D3D12_RESOURCE_STATE_COPY_DEST);
    if (texture)
    {
        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource = upload;
        source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        source.PlacedFootprint = footprint;
        D3D12_TEXTURE_COPY_LOCATION destination{};
        destination.pResource = native->get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        batch.commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    }
    else
    {
        batch.commands->CopyBufferRegion(native->get(), 0, upload, 0, bytes.size());
    }

    barrier(*batch.commands.Get(), native->get(), D3D12_RESOURCE_STATE_COPY_DEST,
            native_state(resource_state::shader_read, texture));
    return submit(std::move(batch));
}

/// @brief Schedule an explicit native state change and extend the resource lifetime.
std::uint64_t native_resources::transition(const std::shared_ptr<gpu_resource>& resource,
                                           resource_state before, resource_state after)
{
    auto batch = begin(resource);
    const auto native = std::static_pointer_cast<native_resource>(resource);
    const bool texture = native->description.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    barrier(*batch.commands.Get(), native->get(), native_state(before, texture),
            native_state(after, texture));
    return submit(std::move(batch));
}
} // namespace atlas::renderer::detail
