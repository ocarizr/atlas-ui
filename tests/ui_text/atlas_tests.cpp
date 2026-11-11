/** @file @brief Validate atlas packing without a native font or graphics device. */
#include <array>
#include <atlas/ui_text/glyph_atlas.hpp>
#include <atlas/ui_text/paint.hpp>
#include <gtest/gtest.h>

namespace atlas::ui_text::tests
{
namespace
{
/// @brief Build a small label with one distinct reusable mask and two placements.
text_layout sample(std::uint8_t alpha)
{
    text_layout layout;
    layout.bitmaps.push_back({2, 2, {alpha, alpha, alpha, alpha}});
    layout.glyphs = {{{0, 0, 2, 2}, 0}, {{3, 0, 2, 2}, 0}};
    return layout;
}
} // namespace

/// @brief Equal masks share one region, and transparent white gutters isolate samples.
TEST(glyph_atlas, DeduplicatesMasksAcrossLabels)
{
    const std::array labels{sample(90), sample(90)};
    const auto atlas = glyph_atlas::pack(labels, {4, 1});
    ASSERT_EQ(atlas.pages().size(), 1U);
    ASSERT_EQ(atlas.label(0).size(), 2U);
    EXPECT_EQ(atlas.label(0)[0].uv, atlas.label(1)[1].uv);
    const auto& pixels = atlas.pages()[0].rgba;
    EXPECT_EQ(pixels[0], std::byte{255});
    EXPECT_EQ(pixels[3], std::byte{0});
    EXPECT_EQ(pixels[23], std::byte{90});
}

/// @brief Distinct masks respect the page quota and invalid input never publishes a partial atlas.
TEST(glyph_atlas, RejectsPagePressureAndInvalidMask)
{
    std::array labels{sample(90), sample(91)};
    EXPECT_THROW((void)glyph_atlas::pack(labels, {4, 1}), std::overflow_error);
    const auto atlas = glyph_atlas::pack(labels, {4, 2});
    EXPECT_EQ(atlas.pages().size(), 2U);
    labels[1].bitmaps[0].coverage.clear();
    EXPECT_THROW((void)glyph_atlas::pack(labels, {4, 2}), std::invalid_argument);
}

/// @brief Viewport placement preserves tint and uses the caller's texture namespace.
TEST(glyph_atlas, EmitsOnlyVisibleGlyphQuads)
{
    const std::array labels{sample(120)};
    const auto atlas = glyph_atlas::pack(labels, {4, 1});
    const auto quads = glyph_quads(atlas, {.origin = {5, 2},
                                           .clip = {0, 0, 8, 8},
                                           .color = {1, 0, 0, 0.5f},
                                           .first_texture_key = 55});
    ASSERT_EQ(quads.size(), 1U);
    EXPECT_EQ(quads[0].bounds.x, 5.f);
    EXPECT_EQ(quads[0].texture, 55U);
    EXPECT_EQ(quads[0].vertices[0].color[3], 0.5f);
}

/// @brief The public request guard applies before platform dispatch.
TEST(text_requests, RejectsInvalidDpiAndProvidesAnEmptyLayout)
{
    EXPECT_THROW((void)shape({.text = "Atlas", .dpi = 0}), std::invalid_argument);
    EXPECT_EQ(shape({}).lines, 0U);
    if (!available())
    {
        EXPECT_THROW((void)shape({.text = "Atlas"}), std::runtime_error);
    }
}
} // namespace atlas::ui_text::tests
