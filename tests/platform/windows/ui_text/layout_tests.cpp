/** @file @brief Windows DirectWrite layout reference behavior without image goldens. */
#include <algorithm>
#include <atlas/ui_text/text_layout.hpp>
#include <gtest/gtest.h>

namespace atlas::ui_text::tests
{
/// @brief Logical wrapping is stable across raster densities, while mask coverage grows.
TEST(native_text, WrapsAtLogicalWidthAndRasterizesAtPhysicalDpi)
{
    text_request request{.text = "Inspector properties and layout", .width = 85};
    const auto low = shape(request);
    request.dpi = 192;
    const auto high = shape(request);
    EXPECT_GT(low.lines, 1U);
    EXPECT_EQ(low.lines, high.lines);
    EXPECT_EQ(low.size, high.size);
    ASSERT_FALSE(low.bitmaps.empty());
    ASSERT_EQ(high.bitmaps.size(), low.bitmaps.size());
    EXPECT_GT(high.bitmaps.front().width, low.bitmaps.front().width);
    EXPECT_GT(high.bitmaps.front().height, low.bitmaps.front().height);
    EXPECT_TRUE(std::ranges::any_of(high.bitmaps.front().coverage,
                                    [](auto alpha)
                                    {
                                        return alpha > 0 && alpha < 255;
                                    }));
    request.wrap = false;
    EXPECT_EQ(shape(request).lines, 1U);
}

/// @brief Windows chooses fallback families for missing glyphs and shapes RTL runs visually.
TEST(native_text, ShapesLatinCombiningMarksAndArabicWithFallback)
{
    const auto latin =
        shape({.text = "A\xcc\x81", .family = "Atlas deliberately unavailable font"});
    EXPECT_TRUE(latin.family_substituted);
    EXPECT_EQ(latin.missing_glyphs, 0U);
    EXPECT_EQ(latin.glyphs.size(), 1U);
    const auto mixed =
        shape({.text = "HUD \xd8\xb3\xd9\x84\xd8\xa7\xd9\x85", .family = "Consolas"});
    EXPECT_EQ(mixed.missing_glyphs, 0U);
    EXPECT_GE(mixed.faces, 2U);
    EXPECT_GT(mixed.glyphs.size(), 3U);
    const auto rtl =
        shape({.text = "\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85", .width = 150, .right_to_left = true});
    ASSERT_GE(rtl.glyphs.size(), 2U);
    EXPECT_GT(rtl.glyphs.front().bounds.x, rtl.glyphs.back().bounds.x);
    EXPECT_EQ(rtl.missing_glyphs, 0U);
}

/// @brief Reject invalid source instead of replacing bytes silently.
TEST(native_text, RejectsInvalidUtf8AndHonorsExplicitLineBreaks)
{
    EXPECT_THROW((void)shape({.text = "\xff"}), std::invalid_argument);
    EXPECT_EQ(shape({.text = "First\nSecond"}).lines, 2U);
}
} // namespace atlas::ui_text::tests
