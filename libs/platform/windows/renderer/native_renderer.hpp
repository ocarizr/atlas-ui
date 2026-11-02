/** @file @brief D3D12 implementation state, never exposed to library consumers. */
#pragma once
#include "renderer_backend.hpp"
#include <Windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

namespace atlas::renderer::detail
{
/// @brief Checked HRESULT with operation context.
void check(HRESULT result, const char* operation);
/// @brief COM pointer used only inside the native backend.
template <class T> using com_ptr = Microsoft::WRL::ComPtr<T>;

/// @brief Owner-thread GPU context shared by all its surfaces.
class native_device final : public device_backend
{
  public:
    /// @brief Select an adapter and create a single graphics queue, fence and startup pipeline.
    native_device(device_options options, std::shared_ptr<std::pmr::memory_resource> memory);
    /// @brief Release retained resources on the owner thread after outstanding work completes.
    ~native_device() override;

    /// @brief Report the selected adapter and whether native debug validation is active.
    device_info info() const override
    {
        return information;
    }

    /// @brief Signal and wait for all earlier commands on the graphics queue.
    void wait_idle() override;
    /// @brief Count D3D12 corruption and error diagnostics when validation is enabled.
    std::uint64_t errors() const override;
    std::uint64_t signal();         ///< Advance the shared queue timeline.
    void wait(std::uint64_t value); ///< Bounded CPU coordinator wait.
    void drain_noexcept() noexcept; ///< Fatal on timeout of a live device, avoiding unsafe resource
                                    ///< release.
    device_options options;         ///< Native initialization policy.
    std::shared_ptr<std::pmr::memory_resource>
        memory;                            ///< Pins caller-selected CPU allocation resource.
    device_info information;               ///< Observable selected backend.
    com_ptr<IDXGIFactory6> factory;        ///< DXGI factory.
    com_ptr<ID3D12Device> gpu;             ///< D3D12 device.
    com_ptr<ID3D12CommandQueue> queue;     ///< Exclusive graphics submission queue.
    com_ptr<ID3D12Fence> fence;            ///< Monotonic queue completion timeline.
    com_ptr<ID3D12RootSignature> root;     ///< Empty foundation root signature.
    com_ptr<ID3D12PipelineState> pipeline; ///< Builtin triangle pipeline.
    HANDLE event = nullptr;                ///< Fence completion event.
    std::uint64_t next_fence = 0;          ///< Last allocated fence value.
};

void create_triangle_pipeline(
    native_device& context); ///< Compile builtin shaders once from memory.

/// @brief One independently fenced backbuffer allocator.
struct frame_slot
{
    com_ptr<ID3D12CommandAllocator> allocator; ///< Reused only after its fence completes.
    com_ptr<ID3D12Resource> buffer;            ///< Swapchain-owned render target.
    std::uint64_t fence = 0;                   ///< Last submission using this allocator.
};

/// @brief Flip-model presentation surface with bounded frame slots.
class native_surface final : public surface_backend
{
  public:
    /// @brief Create a flip-model swapchain with individually fenced frame allocators.
    native_surface(std::shared_ptr<native_device> context,
                   std::shared_ptr<window::detail::window_backend> target, surface_options options);
    /// @brief Release retained resources on the owner thread after outstanding work completes.
    ~native_surface() override;
    /// @brief Update physical extents, suspending zero-size rendering and draining before buffer
    /// replacement.
    void resize(surface_extent extent) override;
    /// @brief Reuse only completed frame slots, then record, signal and present.
    frame_result render(const frame_description& frame) override;
    /// @brief Synchronize and return tightly packed CPU bytes from the last rendered frame.
    frame_image readback() override;

    /// @brief Reject access outside the single graphics coordinator thread.
    std::uint64_t submitted() const override
    {
        context->check_owner();
        return frame_count;
    }

    void create_views(); ///< Retrieve buffers and create RTVs.
    D3D12_CPU_DESCRIPTOR_HANDLE rtv(std::uint32_t index) const; ///< Indexed RTV handle.
    void transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
                    D3D12_RESOURCE_STATES after);           ///< Record barrier.
    std::shared_ptr<native_device> context;                 ///< Retained GPU owner.
    std::shared_ptr<window::detail::window_backend> target; ///< Retained native window.
    surface_options options;                                ///< Presentation buffering policy.
    surface_extent extent;                       ///< Requested dimensions, possibly suspended.
    surface_extent backing_extent;               ///< Allocated backbuffer dimensions.
    com_ptr<IDXGISwapChain3> swapchain;          ///< Flip-model swapchain.
    com_ptr<ID3D12DescriptorHeap> views;         ///< RTV descriptors.
    com_ptr<ID3D12GraphicsCommandList> commands; ///< Exclusive coordinator recording list.
    std::pmr::vector<frame_slot> frames;         ///< CPU bookkeeping using injected PMR.
    std::uint32_t descriptor_size = 0;           ///< Native RTV descriptor stride.
    std::uint32_t last_index = 0;                ///< Last submitted backbuffer.
    bool has_frame = false;                      ///< Whether a diagnostic capture is valid.
    std::uint64_t frame_count = 0;               ///< Executed render frames.
};
} // namespace atlas::renderer::detail
