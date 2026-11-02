/** @file @brief Coordinator-owned bounded pools of exclusive command-recording contexts. */
#include "native_passes.hpp"

namespace atlas::renderer::detail
{
namespace
{
/// @brief Allocate an independent list/allocator and CPU attachment heaps before scheduling.
void initialize(pass_batch& batch, native_device& device, std::uint32_t capacity)
{
    auto* gpu = device.gpu.Get();
    check(
        gpu->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&batch.allocator)),
        "create recording allocator");
    check(gpu->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, batch.allocator.Get(), nullptr,
                                 IID_PPV_ARGS(&batch.commands)),
          "create recording list");
    check(batch.commands->Close(), "close initial recording list");
    D3D12_DESCRIPTOR_HEAP_DESC heap{};
    heap.NumDescriptors = capacity;
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    check(gpu->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&batch.colors)),
          "create recording RTV heap");
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    check(gpu->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&batch.depths)),
          "create recording DSV heap");
}
} // namespace

/// @brief Reserve frame metadata so retention cannot allocate after GPU execution.
native_passes::native_passes(std::shared_ptr<native_resources> store, pass_limits budgets)
    : resources(std::move(store)), limits(budgets), pending(resources->memory()),
      reusable(resources->memory())
{
    pending.reserve(limits.max_frames);
    reusable.reserve(limits.max_frames);
}

/// @brief Move completed frames to the reusable pool without waiting for the GPU.
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

        if (const auto sample = iterator->timer.collect())
        {
            ++timings.completed_frames;
            timings.last_duration_us = *sample;
        }

        reusable.push_back(std::move(*iterator));
        iterator = pending.erase(iterator);
    }
}

/// @brief Reset contexts only after their shared fence has completed.
pass_frame native_passes::begin(std::size_t count)
{
    collect();
    if (pending.size() >= limits.max_frames)
    {
        throw std::overflow_error("render frame slots exhausted; collect completed work");
    }

    pass_frame frame{resources->memory()};
    if (!reusable.empty())
    {
        frame = std::move(reusable.back());
        reusable.pop_back();
    }

    frame.batches.reserve(limits.max_recording_batches);
    while (frame.batches.size() < count)
    {
        frame.batches.emplace_back();
        initialize(frame.batches.back(), *resources->context, limits.max_passes);
    }

    for (std::size_t index = 0; index < count; ++index)
    {
        auto& batch = frame.batches[index];
        check(batch.allocator->Reset(), "reset completed recording allocator");
        check(batch.commands->Reset(batch.allocator.Get(), nullptr),
              "reset completed recording list");
    }

    return frame;
}
} // namespace atlas::renderer::detail
