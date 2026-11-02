/** @file @brief Explicit diagnostic GPU-to-CPU transfer with row-pitch handling. */
#include "native_renderer.hpp"
#include <cstring>

namespace atlas::renderer::detail
{
/// @brief Reject access outside the single graphics coordinator thread.
frame_image native_surface::readback()
{
    context->check_owner();
    if (!has_frame)
    {
        throw std::logic_error("readback requires a submitted frame since the last resize");
    }

    context->wait_idle();
    auto& slot = frames[last_index];
    const auto description = slot.buffer->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 size = 0;
    context->gpu->GetCopyableFootprints(&description, 0, 1, 0, &footprint, nullptr, nullptr, &size);
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buffer{};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = size;
    buffer.Height = 1;
    buffer.DepthOrArraySize = 1;
    buffer.MipLevels = 1;
    buffer.SampleDesc.Count = 1;
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    com_ptr<ID3D12Resource> staging;
    check(context->gpu->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
                                                D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                IID_PPV_ARGS(&staging)),
          "create readback staging buffer");
    check(slot.allocator->Reset(), "reset completed capture allocator");
    check(commands->Reset(slot.allocator.Get(), nullptr), "begin readback commands");
    transition(slot.buffer.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION source{};
    source.pResource = slot.buffer.Get();
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION destination{};
    destination.pResource = staging.Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint = footprint;
    commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    transition(slot.buffer.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT);
    check(commands->Close(), "finish readback commands");
    ID3D12CommandList* lists[]{commands.Get()};
    context->queue->ExecuteCommandLists(1, lists);
    // Keep staging alive even when a bounded wait throws on a live device.
    try
    {
        slot.fence = context->signal();
        context->wait(slot.fence);
    }
    catch (...)
    {
        context->drain_noexcept();
        throw;
    }

    frame_image image{backing_extent,
                      std::vector<std::uint8_t>(static_cast<std::size_t>(backing_extent.width) *
                                                backing_extent.height * 4)};
    void* mapped = nullptr;
    const D3D12_RANGE read_range{0, static_cast<SIZE_T>(size)};
    check(staging->Map(0, &read_range, &mapped), "map completed readback");
    const auto row_size = static_cast<std::size_t>(backing_extent.width) * 4;
    for (std::uint32_t row = 0; row < backing_extent.height; ++row)
    {
        std::memcpy(image.rgba.data() + row * row_size,
                    static_cast<const std::byte*>(mapped) + footprint.Offset +
                        static_cast<std::size_t>(row) * footprint.Footprint.RowPitch,
                    row_size);
    }

    const D3D12_RANGE written{0, 0};
    staging->Unmap(0, &written);
    return image;
}
} // namespace atlas::renderer::detail
