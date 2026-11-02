/** @file @brief Native GPU allocations and retained transfer batches. */
#pragma once
#include "native_renderer.hpp"
#include "resource_backend.hpp"

namespace atlas::renderer::detail
{
/// @brief Single owner of both the resource and its D3D12MA placement range.
class native_resource final : public gpu_resource
{
  public:
    com_ptr<D3D12MA::Allocation> allocation; ///< Owns the resource without a second COM reference.
    D3D12_RESOURCE_DESC description{};       ///< Native copy/allocation description.

    ID3D12Resource* get() const
    {
        return allocation->GetResource();
    } ///< Borrow allocation-owned object.
};

/// @brief All command resources remain alive until the protecting queue fence completes.
struct transfer_batch
{
    com_ptr<ID3D12CommandAllocator> allocator;   ///< Exclusive recording allocator.
    com_ptr<ID3D12GraphicsCommandList> commands; ///< Recorded transfer commands.
    std::shared_ptr<gpu_resource> destination;   ///< Retained destination allocation.
    com_ptr<D3D12MA::Allocation> staging;        ///< Optional upload allocation.
    std::uint64_t fence = 0;                     ///< Protects every object in this batch.
    std::uint64_t bytes = 0;                     ///< Charged upload footprint bytes.
};

/// @brief Bounded upload queue and shader-visible descriptor heap on the device coordinator.
class native_resources final : public resource_backend
{
  public:
    /// @brief Reserve transfer metadata and create a bounded shader-visible descriptor heap.
    native_resources(std::shared_ptr<native_device> device, resource_limits limits);

    /// @brief Drain retained GPU work before releasing native ownership on the coordinator.
    ~native_resources() override
    {
        drain();
    }

    /// @brief Query the native aligned allocation requirement used for byte admission.
    std::uint64_t requirements(buffer_description description) const override;
    /// @brief Query the native aligned allocation requirement used for byte admission.
    std::uint64_t requirements(texture_description description) const override;
    /// @brief Allocate only after slot and aligned-byte admission checks have succeeded.
    std::shared_ptr<gpu_resource> create(buffer_description description) override;
    /// @brief Allocate only after slot and aligned-byte admission checks have succeeded.
    std::shared_ptr<gpu_resource> create(texture_description description) override;
    /// @brief Write the texture SRV into a previously reserved native descriptor slot.
    void view(const std::shared_ptr<gpu_resource>& resource, std::uint32_t index) override;
    /// @brief Copy exact source bytes into retained staging and publish the protecting queue fence.
    std::uint64_t upload(const std::shared_ptr<gpu_resource>& resource, resource_state before,
                         std::span<const std::byte> bytes) override;
    /// @brief Schedule an explicit native state change and extend the resource lifetime.
    std::uint64_t transition(const std::shared_ptr<gpu_resource>& resource, resource_state before,
                             resource_state after) override;
    /// @brief Copy initialized GPU data to CPU bytes and restore the scheduled resource state.
    std::vector<std::byte> readback(const std::shared_ptr<gpu_resource>& resource,
                                    resource_state before) override;
    /// @brief Observe queue completion and reject device-removal sentinels.
    std::uint64_t completed() const override;

    /// @brief Observe the latest value issued on the one device queue timeline.
    std::uint64_t issued() const override
    {
        return context->next_fence;
    }

    /// @brief Wait only on the coordinator with the device diagnostic timeout.
    void wait(std::uint64_t value) override
    {
        context->wait(value);
    }

    /// @brief Reclaim completed staging and retired slots without blocking for GPU completion.
    void collect(std::uint64_t complete) override;
    /// @brief Report occupied allocation ranges and staging separately from allocator heap
    /// reservation.
    resource_statistics statistics() const override;

    /// @brief Borrow the CPU PMR resource retained by the device.
    std::pmr::memory_resource* memory() const override
    {
        return context->memory.get();
    }

    /// @brief Complete queued GPU use before releasing retained native storage.
    void drain() noexcept override
    {
        context->drain_noexcept();
    }

    transfer_batch begin(const std::shared_ptr<gpu_resource>&
                             destination); ///< Admit recording resources before submission.
    std::uint64_t
    submit(transfer_batch batch); ///< Signal and retain with no post-execute allocation.
    com_ptr<D3D12MA::Allocation>
    allocate(const D3D12_RESOURCE_DESC& description, D3D12_HEAP_TYPE heap,
             D3D12_RESOURCE_STATES state) const; ///< Allocate through D3D12MA.
    std::shared_ptr<native_device> context;      ///< Queue and allocator lifetime.
    resource_limits limits;                      ///< Immutable capacity/budget policy.
    com_ptr<ID3D12DescriptorHeap> descriptors;   ///< One bounded shader-visible SRV heap.
    std::pmr::vector<transfer_batch> pending;    ///< Capacity reserved before any submission.
    std::pmr::vector<transfer_batch>
        reusable;                    ///< Completed allocators/lists, reset only after their fence.
    std::uint64_t staging_bytes = 0; ///< In-flight logical upload footprint bytes.
};

D3D12_RESOURCE_DESC buffer_desc(std::uint64_t size); ///< Describe a byte-addressed buffer.
D3D12_RESOURCE_DESC texture_desc(texture_description description); ///< Describe one RGBA8 mip.
D3D12_RESOURCE_STATES native_state(resource_state state,
                                   bool texture); ///< Translate portable scheduled states.
void barrier(ID3D12GraphicsCommandList& commands, ID3D12Resource* resource,
             D3D12_RESOURCE_STATES before,
             D3D12_RESOURCE_STATES after); ///< Skip redundant whole-resource barriers.
} // namespace atlas::renderer::detail
