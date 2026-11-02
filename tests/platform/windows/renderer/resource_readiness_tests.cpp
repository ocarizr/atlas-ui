/** @file @brief Data readiness and allocation-retirement fences serve distinct contracts. */
#include "resource_test_support.hpp"

namespace atlas::renderer::tests
{
/// @brief The store pins the native device and CPU allocator beyond their public facade owners.
TEST(resource_store_lifetime_tests, retains_device_and_pmr_through_final_release)
{
    auto memory = std::make_shared<std::pmr::synchronized_pool_resource>();
    std::weak_ptr<std::pmr::memory_resource> retained = memory;
    auto graphics = std::make_unique<device>(device_options{adapter_preference::software}, memory);
    auto store = std::make_unique<resource_store>(*graphics);
    const auto buffer = store->create_buffer({4});
    const std::array data{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    const auto uploaded = store->upload(buffer, data);
    memory.reset();
    graphics.reset();
    EXPECT_FALSE(retained.expired());
    store->wait(uploaded);
    EXPECT_EQ(store->readback(buffer), std::vector<std::byte>(data.begin(), data.end()));
    store.reset();
    EXPECT_TRUE(retained.expired());
}

/// @brief Subsequent GPU reads extend lifetime without undoing a completed initialization upload.
TEST_F(resource_test, completed_upload_remains_ready_during_later_gpu_use)
{
    const auto buffer = resources.create_buffer({4});
    const std::array data{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    resources.wait(resources.upload(buffer, data));
    ASSERT_TRUE(resources.ready(buffer));
    gpu_gate gate{graphics};
    const auto using_resource = resources.transition(buffer, resource_state::copy_source);
    resources.mark_used(buffer, using_resource);
    EXPECT_TRUE(resources.ready(buffer));
    EXPECT_FALSE(resources.completed(using_resource));
    resources.release(buffer);
    resources.collect();
    EXPECT_EQ(resources.statistics().retired_resources, 1U);
    gate.open();
    resources.wait(using_resource);
    EXPECT_EQ(resources.statistics().retired_resources, 0U);
}
} // namespace atlas::renderer::tests
