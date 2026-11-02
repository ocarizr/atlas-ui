/** @file @brief Retired slots and staging remain unavailable until controlled GPU completion. */
#include "resource_test_support.hpp"

namespace atlas::renderer::tests
{
/// @brief Releasing a handle never permits reuse while queued transfers still reference it.
TEST_F(resource_test, fences_resource_reuse_and_invalidates_stale_handles)
{
    resource_store store{graphics, {1, 1, 1, 65536, 1024}};
    gpu_gate gate{graphics};
    const auto original = store.create_buffer({4});
    const std::array data{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    const auto token = store.upload(original, data);
    EXPECT_FALSE(store.ready(original));
    store.release(original);
    store.collect();
    EXPECT_THROW(store.state(original), std::invalid_argument);
    EXPECT_THROW(store.create_buffer({4}), std::overflow_error);
    EXPECT_EQ(store.statistics().retired_resources, 1U);
    EXPECT_EQ(store.statistics().staging_bytes, 4U);
    gate.open();
    store.wait(token);
    EXPECT_EQ(store.statistics().allocation_bytes, 0U);
    EXPECT_EQ(store.statistics().staging_bytes, 0U);
    const auto reused = store.create_buffer({4});
    EXPECT_EQ(reused.index, original.index);
    EXPECT_NE(reused.generation, original.generation);
    EXPECT_THROW(store.upload(original, data), std::invalid_argument);
}

/// @brief Live and retired views keep their parent texture allocation and slots pinned.
TEST_F(resource_test, descriptors_retain_texture_until_their_last_use_completes)
{
    resource_store store{graphics, {1, 1, 1, 65536, 1024}};
    gpu_gate gate{graphics};
    const auto texture = store.create_texture({1, 1});
    const auto view = store.create_view(texture);
    const std::array pixel{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{255}};
    const auto token = store.upload(texture, pixel);
    store.mark_used(view, token);
    store.release(texture);
    store.collect();
    EXPECT_THROW(store.create_texture({1, 1}), std::overflow_error);
    EXPECT_THROW(store.create_view(texture), std::invalid_argument);
    store.release(view);
    store.collect();
    EXPECT_EQ(store.statistics().retired_descriptors, 1U);
    gate.open();
    store.wait(token);
    const auto replacement = store.create_texture({1, 1});
    const auto next_view = store.create_view(replacement);
    EXPECT_NE(view.generation, next_view.generation);
    EXPECT_THROW(store.mark_used(view, token), std::invalid_argument);
}

/// @brief In-flight staging and transfer quotas fail without admitting another command batch.
TEST_F(resource_test, bounds_pending_uploads_and_reclaims_recording_slots)
{
    resource_store store{graphics, {2, 1, 1, 131072, 8}};
    gpu_gate gate{graphics};
    const auto first = store.create_buffer({4});
    const auto second = store.create_buffer({4});
    const std::array data{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    const auto token = store.upload(first, data);
    EXPECT_THROW(store.upload(second, data), std::overflow_error);
    EXPECT_EQ(store.statistics().pending_transfers, 1U);
    gate.open();
    store.wait(token);
    store.wait(store.upload(second, data));
    EXPECT_EQ(store.readback(second), std::vector<std::byte>(data.begin(), data.end()));
}

/// @brief Foreign identities and byte exhaustion cannot consume a slot or mutate an existing
/// resource.
TEST_F(resource_test, enforces_allocation_and_descriptor_budgets)
{
    resource_store store{graphics, {4, 1, 2, 65536, 1}};
    const auto buffer = store.create_buffer({4});
    EXPECT_THROW(store.create_buffer({1}), std::length_error);
    EXPECT_THROW(resources.state(buffer), std::invalid_argument);
    const std::array data{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    EXPECT_THROW(store.upload(buffer, data), std::length_error);
    EXPECT_FALSE(store.ready(buffer));
    EXPECT_EQ(store.statistics().pending_transfers, 0U);
    EXPECT_EQ(store.statistics().live_resources, 1U);
    const auto texture = resources.create_texture({1, 1});
    resources.create_view(texture);
    resources.create_view(texture);
    EXPECT_THROW(resources.create_view(texture), std::overflow_error);
}
} // namespace atlas::renderer::tests
