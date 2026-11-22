/** @file @brief Scrollbar proportional sizing, zero-range policies and sparse chrome publication.
 */
#include "scroll_interaction_support.hpp"
#include <limits>

namespace atlas::ui::tests
{
/// @brief Pure horizontal thumb geometry uses the same range and endpoints as vertical chrome.
TEST(scrollbar_geometry_test, ProportionalThumbUsesFiniteClampedTravel)
{
    scroll_geometry source{{0, 0, 100, 80}, {300, 80}, {100, 0}, {200, 0}};
    scrollbar_style style;
    style.axis = scroll_axis::horizontal;
    const auto result = resolve_scrollbar({0, 0, 120, 10}, source, style);
    EXPECT_EQ(result.thumb, (layout_rect{40, 0, 40, 10}));
    EXPECT_TRUE(result.visible);
    EXPECT_TRUE(result.enabled);
    source.offset.x = 200;
    EXPECT_EQ(resolve_scrollbar({0, 0, 120, 10}, source, style).thumb,
              (layout_rect{80, 0, 40, 10}));
    style.minimum_thumb = 120;
    EXPECT_FALSE(resolve_scrollbar({0, 0, 120, 10}, source, style).enabled);
}

/// @brief Empty or fitting content cannot create NaN or a draggable zero-range thumb.
TEST(scrollbar_geometry_test, ZeroRangesHideOrDisableWithoutDivision)
{
    scrollbar_style style;
    auto result = resolve_scrollbar({0, 0, 10, 80}, {}, style);
    EXPECT_FALSE(result.visible);
    EXPECT_FALSE(result.enabled);
    EXPECT_EQ(result.thumb, result.track);
    style.hide_when_zero = false;
    result = resolve_scrollbar({0, 0, 10, 80}, {}, style);
    EXPECT_TRUE(result.visible);
    EXPECT_FALSE(result.enabled);
    EXPECT_EQ(resolve_scrollbar({}, {}, style).thumb, layout_rect{});
    style.minimum_thumb = std::numeric_limits<float>::quiet_NaN();
    EXPECT_THROW(resolve_scrollbar({}, {}, style), std::invalid_argument);
}

/// @brief Chrome contributes a track and thumb primitive, sharing the existing painter context.
TEST_F(scroll_navigation_fixture, PaintAndInputUseTheSameSparseThumbGeometry)
{
    list();
    const auto bar = chrome();
    const auto source = tree.snapshot();
    const auto geometry = layout.calculate(source);
    ASSERT_EQ(source.scrollbars().size(), 1U);
    ASSERT_EQ(geometry.scrollbars().size(), 1U);
    EXPECT_EQ(geometry.scrollbars()[0].geometry.thumb, (layout_rect{90, 0, 10, 32}));
    const auto result = paint.calculate(source, geometry);
    ASSERT_GE(result.quads().size(), 2U);
    EXPECT_EQ(result.quads().back().target, bar);
    EXPECT_EQ(result.quads().back().bounds, geometry.scrollbars()[0].geometry.thumb);
    tree.reset_scrollbar(bar);
    EXPECT_TRUE(layout.calculate(tree.snapshot()).scrollbars().empty());
    EXPECT_EQ(layout.statistics().measured_nodes, 0U);
}

/// @brief Removed owners do not alias reused slots or leave active chrome hit regions.
TEST_F(scroll_navigation_fixture, RemovedViewportHidesItsChromeAndRetainedPacketsStayValid)
{
    list();
    const auto bar = chrome();
    const auto source = tree.snapshot();
    const auto retained = layout.calculate(source);
    tree.remove(viewport);
    tree.create();
    const auto next = layout.calculate(tree.snapshot());
    ASSERT_EQ(next.scrollbars().size(), 1U);
    EXPECT_FALSE(next.scrollbars()[0].geometry.visible);
    EXPECT_TRUE(retained.scrollbars()[0].geometry.enabled);
    EXPECT_EQ(retained.scrollbars()[0].target, bar);
}

/// @brief Structural edits cannot turn external chrome into an active part of its own content.
TEST_F(scroll_navigation_fixture, ReparentedChromeInsideItsViewportBecomesInert)
{
    list();
    const auto bar = chrome();
    tree.reparent(bar, content);
    const auto result = layout.calculate(tree.snapshot());
    ASSERT_EQ(result.scrollbars().size(), 1U);
    EXPECT_FALSE(result.scrollbars()[0].geometry.visible);
    EXPECT_FALSE(result.scrollbars()[0].geometry.enabled);
}
} // namespace atlas::ui::tests
