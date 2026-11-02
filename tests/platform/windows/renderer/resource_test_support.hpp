/** @file @brief Isolated WARP resource fixtures and deterministic GPU queue gates. */
#pragma once
#include "native_renderer.hpp"
#include <atlas/renderer/resources.hpp>
#include <gtest/gtest.h>

namespace atlas::renderer::tests
{
/// @brief Fresh allocator/registry per case, with validation checked after GPU completion.
class resource_test : public ::testing::Test
{
  protected:
    device graphics{{adapter_preference::software}}; ///< Native WARP coordinator.
    resource_store resources{
        graphics,
        {4, 2, 2, 1024ULL * 1024, 4096}}; ///< Small bounded pools expose exhaustion paths.

    void TearDown() override
    {
        graphics.wait_idle();
        resources.collect();
        EXPECT_EQ(graphics.debug_error_count(), 0U);
    } ///< Drain before validation inspection.
};

/// @brief Hold the queue without sleeps; destruction unblocks it even after failed assertions.
class gpu_gate
{
  public:
    /// @brief Hold the GPU queue behind an independently CPU-signaled test fence.
    explicit gpu_gate(device& graphics)
        : context_(std::static_pointer_cast<detail::native_device>(
              detail::device_access::retain(graphics)))
    {
        detail::check(context_->gpu->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate_)),
                      "create test gate");
        detail::check(context_->queue->Wait(gate_.Get(), 1), "hold test queue");
    }

    ~gpu_gate()
    {
        (void)gate_->Signal(1);
    } ///< Never leave GPU teardown waiting behind a test failure.

    void open()
    {
        detail::check(gate_->Signal(1), "open test queue");
    } ///< Release work deterministically.
  private:
    std::shared_ptr<detail::native_device>
        context_;                       ///< Retained device until the gate is released.
    detail::com_ptr<ID3D12Fence> gate_; ///< Independent CPU-signaled completion gate.
};
} // namespace atlas::renderer::tests
