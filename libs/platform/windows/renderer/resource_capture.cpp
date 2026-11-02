/** @file @brief Diagnostic readback of initialized buffers and tightly packed RGBA textures. */
#include "native_resources.hpp"
#include <cstring>

namespace atlas::renderer::detail
{
/// @brief Copy initialized GPU data to CPU bytes and restore the scheduled resource state.
std::vector<std::byte> native_resources::readback(const std::shared_ptr<gpu_resource>& resource,
                                                  resource_state before)
{
    context->wait_idle();
    collect(completed());
    const auto native = std::static_pointer_cast<native_resource>(resource);
    const bool texture = native->description.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 size = native->description.Width;
    if (texture)
    {
        context->gpu->GetCopyableFootprints(&native->description, 0, 1, 0, &footprint, nullptr,
                                            nullptr, &size);
    }

    auto staging =
        allocate(buffer_desc(size), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    auto batch = begin(resource);
    barrier(*batch.commands.Get(), native->get(), native_state(before, texture),
            D3D12_RESOURCE_STATE_COPY_SOURCE);
    auto* output = staging->GetResource();
    if (texture)
    {
        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource = native->get();
        source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION destination{};
        destination.pResource = output;
        destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        destination.PlacedFootprint = footprint;
        batch.commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    }
    else
    {
        batch.commands->CopyBufferRegion(output, 0, native->get(), 0, size);
    }

    barrier(*batch.commands.Get(), native->get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
            native_state(before, texture));
    // A timeout must never release readback storage while the live GPU can still write to it.
    try
    {
        const auto fence = submit(std::move(batch));
        context->wait(fence);
    }
    catch (...)
    {
        context->drain_noexcept();
        throw;
    }

    const auto row_bytes =
        static_cast<std::size_t>(native->description.Width) * (texture ? 4U : 1U);
    const auto rows = texture ? native->description.Height : 1U;
    /// @brief Own exact readback bytes before mapping native staging storage.
    std::vector<std::byte> result(row_bytes * rows);
    void* mapped = nullptr;
    const D3D12_RANGE read{0, static_cast<SIZE_T>(size)};
    check(output->Map(0, &read, &mapped), "map resource readback");
    for (std::uint32_t row = 0; row < rows; ++row)
    {
        const auto pitch = texture ? footprint.Footprint.RowPitch : row_bytes;
        std::memcpy(result.data() + static_cast<std::size_t>(row) * row_bytes,
                    static_cast<const std::byte*>(mapped) + static_cast<std::size_t>(row) * pitch,
                    row_bytes);
    }

    const D3D12_RANGE written{0, 0};
    output->Unmap(0, &written);
    collect(completed());
    return result;
}
} // namespace atlas::renderer::detail
