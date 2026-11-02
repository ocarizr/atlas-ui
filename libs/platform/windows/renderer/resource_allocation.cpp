/** @file @brief GPU suballocation and shader-resource descriptor creation. */
#include "native_resources.hpp"

namespace atlas::renderer::detail
{
/// @brief Describe a linear byte buffer with native row-major layout.
D3D12_RESOURCE_DESC buffer_desc(std::uint64_t size)
{
    D3D12_RESOURCE_DESC result{};
    result.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    result.Width = size;
    result.Height = 1;
    result.DepthOrArraySize = 1;
    result.MipLevels = 1;
    result.SampleDesc.Count = 1;
    result.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return result;
}

/// @brief Describe a single RGBA8 UNORM mip without UI-specific assumptions.
D3D12_RESOURCE_DESC texture_desc(texture_description description)
{
    auto result = buffer_desc(description.width);
    result.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    result.Height = description.height;
    result.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    result.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    return result;
}

/// @brief Reserve transfer metadata and create a bounded shader-visible descriptor heap.
native_resources::native_resources(std::shared_ptr<native_device> device, resource_limits budgets)
    : context(std::move(device)), limits(budgets), pending(context->memory.get()),
      reusable(context->memory.get())
{
    pending.reserve(limits.max_transfers);
    reusable.reserve(limits.max_transfers);
    D3D12_DESCRIPTOR_HEAP_DESC heap{};
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap.NumDescriptors = limits.max_descriptors;
    heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    check(context->gpu->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&descriptors)),
          "create shader descriptor heap");
}

/// @brief Create a D3D12MA-owned allocation without a redundant resource reference.
com_ptr<D3D12MA::Allocation> native_resources::allocate(const D3D12_RESOURCE_DESC& description,
                                                        D3D12_HEAP_TYPE heap,
                                                        D3D12_RESOURCE_STATES state) const
{
    D3D12MA::ALLOCATION_DESC policy{};
    policy.HeapType = heap;
    com_ptr<D3D12MA::Allocation> result;
    check(context->allocations->CreateResource(&policy, &description, state, nullptr, &result,
                                               IID_NULL, nullptr),
          "allocate GPU resource");
    return result;
}

/// @brief Query the native aligned allocation requirement used for byte admission.
std::uint64_t native_resources::requirements(buffer_description description) const
{
    const auto native = buffer_desc(description.size);
    return context->gpu->GetResourceAllocationInfo(0, 1, &native).SizeInBytes;
}

/// @brief Query the native aligned allocation requirement used for byte admission.
std::uint64_t native_resources::requirements(texture_description description) const
{
    const auto native = texture_desc(description);
    return context->gpu->GetResourceAllocationInfo(0, 1, &native).SizeInBytes;
}

/// @brief Allocate only after slot and aligned-byte admission checks have succeeded.
std::shared_ptr<gpu_resource> native_resources::create(buffer_description description)
{
    auto result = std::make_shared<native_resource>();
    result->description = buffer_desc(description.size);
    result->allocation =
        allocate(result->description, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);
    return result;
}

/// @brief Allocate only after slot and aligned-byte admission checks have succeeded.
std::shared_ptr<gpu_resource> native_resources::create(texture_description description)
{
    auto result = std::make_shared<native_resource>();
    result->description = texture_desc(description);
    result->allocation =
        allocate(result->description, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);
    return result;
}

/// @brief Write the texture SRV into a previously reserved native descriptor slot.
void native_resources::view(const std::shared_ptr<gpu_resource>& resource, std::uint32_t index)
{
    const auto native = std::static_pointer_cast<native_resource>(resource);
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Texture2D.MipLevels = 1;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    auto handle = descriptors->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(index) * context->gpu->GetDescriptorHandleIncrementSize(
                                                   D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    context->gpu->CreateShaderResourceView(native->get(), &view, handle);
}

/// @brief Observe queue completion and reject device-removal sentinels.
std::uint64_t native_resources::completed() const
{
    context->check_owner();
    const auto value = context->fence->GetCompletedValue();
    if (value == UINT64_MAX)
    {
        check(context->gpu->GetDeviceRemovedReason(), "resource device removed");
        throw std::runtime_error("invalid GPU completion value");
    }

    return value;
}

/// @brief Reclaim completed staging and retired slots without blocking for GPU completion.
void native_resources::collect(std::uint64_t complete)
{
    for (auto iterator = pending.begin(); iterator != pending.end();)
    {
        if (iterator->fence > complete)
        {
            ++iterator;
            continue;
        }

        staging_bytes -= iterator->bytes;
        iterator->destination.reset();
        iterator->staging.Reset();
        iterator->bytes = 0;
        reusable.push_back(std::move(*iterator));
        iterator = pending.erase(iterator);
    }
}

/// @brief Report occupied allocation ranges and staging separately from allocator heap reservation.
resource_statistics native_resources::statistics() const
{
    D3D12MA::Budget local{}, remote{};
    context->allocations->GetBudget(&local, &remote);
    resource_statistics result;
    result.pending_transfers = pending.size();
    result.staging_bytes = staging_bytes;
    result.reserved_heap_bytes = local.Stats.BlockBytes + remote.Stats.BlockBytes;
    return result;
}

/// @brief Retain the device and construct its selected native resource adapter.
std::shared_ptr<resource_backend> make_resource_backend(std::shared_ptr<device_backend> device,
                                                        resource_limits limits)
{
    return std::make_shared<native_resources>(
        std::static_pointer_cast<native_device>(std::move(device)), limits);
}
} // namespace atlas::renderer::detail
