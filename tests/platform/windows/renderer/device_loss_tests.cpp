/** @file @brief Device loss is typed and terminal, while teardown releases invalid native
 * resources. */
#include "native_renderer.hpp"
#include <atlas/renderer/resources.hpp>
#include <gtest/gtest.h>

namespace atlas::renderer::tests
{
/// @brief Explicit D3D12 removal is isolated to this test device, not the physical adapter.
TEST(device_loss, reports_original_reason_and_rejects_further_resource_use)
{
    device graphics{{adapter_preference::software}};
    resource_store resources{graphics};
    const auto buffer = resources.create_buffer({4});
    EXPECT_FALSE(graphics.status().lost());
    auto native =
        std::static_pointer_cast<detail::native_device>(detail::device_access::retain(graphics));
    detail::com_ptr<ID3D12Device5> removable;
    ASSERT_TRUE(SUCCEEDED(native->gpu.As(&removable)));
    removable->RemoveDevice();
    EXPECT_TRUE(graphics.status().lost());
    EXPECT_NE(graphics.status().native_code, 0U);
    EXPECT_THROW(graphics.wait_idle(), device_lost);
    EXPECT_THROW(resources.ready(buffer), device_lost);
}

/// @brief UINT64_MAX is a removal sentinel and must never be emitted as an ordinary fence value.
TEST(device_loss, rejects_fence_timeline_exhaustion_before_signal)
{
    device graphics{{adapter_preference::software}};
    auto native =
        std::static_pointer_cast<detail::native_device>(detail::device_access::retain(graphics));
    const auto previous = native->next_fence;
    native->next_fence = UINT64_MAX - 1;
    EXPECT_THROW(native->signal(), std::overflow_error);
    native->next_fence = previous;
}
} // namespace atlas::renderer::tests
