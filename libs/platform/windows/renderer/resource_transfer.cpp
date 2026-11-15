/** @file @brief Bounded full-resource uploads and explicit single-queue state transitions. */
#include "native_resources.hpp"
#include <cstring>

namespace atlas::renderer::detail
{
namespace
{
/// @brief Stack-owned D3D12 padding data for a validated 2D mip chain.
struct upload_layout
{
    std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT, D3D12_REQ_MIP_LEVELS>
        footprints{};                                     ///< Native aligned offsets and pitches.
    std::array<UINT, D3D12_REQ_MIP_LEVELS> rows{};        ///< Row count of each mip.
    std::array<UINT64, D3D12_REQ_MIP_LEVELS> row_bytes{}; ///< Tight row width in bytes.
    UINT64 size = 0; ///< Total required staging bytes, including padding.
    UINT levels = 0; ///< Zero represents a byte buffer.

    /// @brief Copy the caller's tight levels into their native row and placement alignment.
    void pack(std::byte* destination, std::span<const std::byte> source) const
    {
        if (levels == 0)
        {
            std::memcpy(destination, source.data(), source.size());
            return;
        }

        for (UINT level = 0; level < levels; ++level)
        {
            auto* output = destination + footprints.at(level).Offset;
            const auto pitch = footprints.at(level).Footprint.RowPitch;
            const auto tight = static_cast<std::size_t>(row_bytes.at(level));
            for (UINT row = 0; row < rows.at(level); ++row)
            {
                std::memcpy(output + static_cast<std::size_t>(row) * pitch, source.data(), tight);
                source = source.subspan(tight);
            }
        }
    }

    /// @brief Encode one GPU copy per level into the same atomic upload batch.
    void copy(ID3D12GraphicsCommandList& commands, ID3D12Resource& output,
              ID3D12Resource& staging) const
    {
        if (levels == 0)
        {
            commands.CopyBufferRegion(&output, 0, &staging, 0, size);
            return;
        }

        for (UINT level = 0; level < levels; ++level)
        {
            D3D12_TEXTURE_COPY_LOCATION source{};
            source.pResource = &staging;
            source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            source.PlacedFootprint = footprints.at(level);
            D3D12_TEXTURE_COPY_LOCATION destination{};
            destination.pResource = &output;
            destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            destination.SubresourceIndex = level;
            commands.CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        }
    }
};
} // namespace

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
transfer_batch native_resources::begin(const std::shared_ptr<gpu_resource>& destination,
                                       std::uint64_t upload_bytes)
{
    collect(completed());
    if (pending.size() >= limits.max_transfers)
    {
        throw std::overflow_error("GPU transfer slots exhausted");
    }

    if (upload_bytes > limits.max_staging_bytes - staging_bytes)
    {
        throw std::length_error("GPU upload staging budget exhausted");
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
    auto native = std::static_pointer_cast<native_resource>(resource);
    const bool texture = native->description.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    upload_layout layout;
    layout.size = bytes.size();
    if (texture)
    {
        layout.levels = native->description.MipLevels;
        context->gpu->GetCopyableFootprints(&native->description, 0, layout.levels, 0,
                                            layout.footprints.data(), layout.rows.data(),
                                            layout.row_bytes.data(), &layout.size);
    }

    auto batch = begin(resource, layout.size);
    batch.staging = allocate(buffer_desc(layout.size), D3D12_HEAP_TYPE_UPLOAD,
                             D3D12_RESOURCE_STATE_GENERIC_READ);
    batch.bytes = layout.size;
    auto* upload = batch.staging->GetResource();
    void* mapped = nullptr;
    const D3D12_RANGE empty{0, 0};
    check(upload->Map(0, &empty, &mapped), "map upload staging");
    layout.pack(static_cast<std::byte*>(mapped), bytes);

    const D3D12_RANGE written{0, static_cast<SIZE_T>(layout.size)};
    upload->Unmap(0, &written);
    barrier(*batch.commands.Get(), native->get(), native_state(before, texture),
            D3D12_RESOURCE_STATE_COPY_DEST);
    layout.copy(*batch.commands.Get(), *native->get(), *upload);

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
