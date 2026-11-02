/** @file @brief Exact WARP roundtrips, row pitch and explicit resource-state transitions. */
#include "resource_test_support.hpp"
#include <future>
#include <numeric>

namespace atlas::renderer::tests
{
/// @brief Upload snapshots caller data and restores scheduled state after diagnostic copies.
TEST_F(resource_test, buffer_roundtrip_and_reupload_preserve_exact_bytes)
{
    const auto buffer = resources.create_buffer({257});
    EXPECT_FALSE(resources.ready(buffer));
    EXPECT_THROW(resources.readback(buffer), std::logic_error);
    std::vector<std::byte> input(257, std::byte{0x4a});
    EXPECT_THROW(resources.upload(buffer, std::span{input}.first(256)), std::invalid_argument);
    const auto token = resources.upload(buffer, input);
    std::fill(input.begin(), input.end(), std::byte{0x7b});
    resources.wait(token);
    EXPECT_TRUE(resources.ready(buffer));
    EXPECT_EQ(resources.readback(buffer), std::vector<std::byte>(257, std::byte{0x4a}));
    resources.wait(resources.transition(buffer, resource_state::copy_source));
    EXPECT_EQ(resources.state(buffer), resource_state::copy_source);
    resources.wait(resources.upload(buffer, input));
    EXPECT_EQ(resources.state(buffer), resource_state::shader_read);
    EXPECT_EQ(resources.readback(buffer), input);
}

/// @brief Non-aligned RGBA rows cross native 256-byte pitch boundaries without changing texels.
TEST_F(resource_test, texture_roundtrip_handles_row_pitch)
{
    const auto texture = resources.create_texture({67, 3});
    const auto view = resources.create_view(texture);
    std::vector<std::byte> rgba(std::size_t{67} * 3 * 4);
    for (std::size_t index = 0; index < rgba.size(); ++index)
    {
        rgba[index] = static_cast<std::byte>(index % 251);
    }

    const auto uploaded = resources.upload(texture, rgba);
    resources.wait(uploaded);
    EXPECT_TRUE(resources.completed(uploaded));
    EXPECT_TRUE(resources.ready(texture));
    resources.wait(resources.transition(texture, resource_state::copy_source));
    EXPECT_EQ(resources.readback(texture), rgba);
    EXPECT_EQ(resources.state(texture), resource_state::copy_source);
    resources.mark_used(view, uploaded);
}

/// @brief Registry operations reject foreign threads before touching native objects.
TEST_F(resource_test, enforces_coordinator_and_foreign_token_provenance)
{
    const auto buffer = resources.create_buffer({4});
    EXPECT_THROW(resources.wait({9999, 1}), std::invalid_argument);
    auto task = std::async(std::launch::async,
                           [this, buffer]
                           {
                               EXPECT_THROW(resources.state(buffer), std::logic_error);
                               EXPECT_THROW(resources.collect(), std::logic_error);
                           });
    task.get();
}
} // namespace atlas::renderer::tests
