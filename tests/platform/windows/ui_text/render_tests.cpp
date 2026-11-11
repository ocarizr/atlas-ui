/** @file @brief WARP pixel and GPU fence checks for real shaped text and atlas pages. */
#include <atlas/ui_text/paint.hpp>
#include <gtest/gtest.h>

namespace atlas::ui_text::tests
{
/// @brief Draw a real label, retire its atlas immediately, and verify readback after its fence.
TEST(native_text, DrawsThroughOrderedPaintAndRetiresAtlasAfterSubmission)
{
    renderer::device device{{renderer::adapter_preference::software}};
    renderer::resource_store resources{device};
    renderer::pass_renderer passes{resources};
    ui_renderer::paint_renderer painter{resources, passes};
    const auto target = resources.create_texture({128, 48, renderer::texture_role::color_target});
    const std::array labels{shape({.text = "Atlas", .size = 24})};
    const auto packed = glyph_atlas::pack(labels, {64, 2});
    auto atlas = std::make_unique<uploaded_atlas>(resources, packed, 900);
    device.wait_idle();
    ASSERT_TRUE(atlas->ready());
    auto quads =
        glyph_quads(packed, {.origin = {4, 4}, .clip = {0, 0, 46, 48}, .first_texture_key = 900});
    ASSERT_FALSE(quads.empty());
    auto offscreen = quads.front();
    offscreen.clip = {130, 0, 20, 48};
    offscreen.texture = 999;
    quads.push_back(offscreen);
    const auto fence =
        painter.submit_quads({{0, 0, 128, 48}, quads}, target, {128, 48}, atlas->bindings());
    atlas.reset();
    resources.wait(fence);
    const auto pixels = resources.readback(target);
    std::size_t bright = 0;
    for (std::size_t i = 0; i < pixels.size(); i += 4)
    {
        if (std::to_integer<int>(pixels[i]) > 100)
        {
            EXPECT_LT((i / 4) % 128, 46U);
            ++bright;
        }
    }

    EXPECT_GT(bright, 20U);
    resources.collect();
    EXPECT_EQ(resources.statistics().live_descriptors, 0U);
    EXPECT_EQ(resources.statistics().retired_descriptors, 0U);
    EXPECT_EQ(device.debug_error_count(), 0U);
}

/// @brief A partially admitted atlas rolls back its page handles after GPU quota failure.
TEST(native_text, RollsBackUploadsUnderDescriptorPressure)
{
    renderer::device device{{renderer::adapter_preference::software}};
    renderer::resource_store resources{device, {.max_descriptors = 1}};
    text_layout label;
    label.bitmaps = {{2, 2, {100, 100, 100, 100}}, {2, 2, {200, 200, 200, 200}}};
    label.glyphs = {{{0, 0, 2, 2}, 0}, {{3, 0, 2, 2}, 1}};
    const auto atlas = glyph_atlas::pack(std::span{&label, 1}, {4, 2});
    ASSERT_EQ(atlas.pages().size(), 2U);
    EXPECT_THROW((uploaded_atlas{resources, atlas, 1}), std::overflow_error);
    device.wait_idle();
    resources.collect();
    EXPECT_EQ(resources.statistics().live_resources, 0U);
    EXPECT_EQ(resources.statistics().retired_resources, 0U);
    EXPECT_EQ(resources.statistics().live_descriptors, 0U);
    EXPECT_EQ(device.debug_error_count(), 0U);
}
} // namespace atlas::ui_text::tests
