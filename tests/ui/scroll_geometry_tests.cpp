/** @file @brief Finite viewport measurement, clamping and offset-only layout reuse. */
#include "scroll_test_support.hpp"
#include <limits>

namespace atlas::ui::tests
{
/// @brief Scrolling changes presentation while retaining the numeric content layout.
TEST_F(scroll_fixture, OffsetClampsWithoutNaturalRemeasure)
{
    const auto first = calculate();
    EXPECT_EQ(scrolling(first).viewport, (layout_rect{0, 0, 100, 80}));
    EXPECT_EQ(scrolling(first).extent, (layout_extent{100, 200}));
    EXPECT_EQ(scrolling(first).range, (layout_point{0, 120}));
    offset({900, 900});
    const auto end = calculate();
    EXPECT_EQ(scrolling(end).offset, (layout_point{0, 120}));
    EXPECT_EQ(layout.statistics().measured_nodes, 0U);
    EXPECT_EQ(layout.statistics().arranged_nodes, 0U);
    EXPECT_EQ(end.find(content), first.find(content));
    EXPECT_EQ(scrolling(first).offset, (layout_point{}));
    EXPECT_NE(end.revision(), first.revision());
}

/// @brief Content changes and viewport changes reclamp using the same requested offset.
TEST_F(scroll_fixture, ResizeAndContentChangesReclampAtPublication)
{
    offset({0, 90});
    EXPECT_EQ(scrolling(calculate()).offset.y, 90);
    resize_content({100, 100});
    EXPECT_EQ(scrolling(calculate()).offset.y, 20);
    tree.set_viewport({0, 0, 100, 120});
    EXPECT_EQ(scrolling(calculate()).offset.y, 0);
    resize_content({100, 240});
    EXPECT_EQ(scrolling(calculate()).offset.y, 90);
    offset({0, -25});
    EXPECT_EQ(scrolling(calculate()).offset.y, 0);
}

/// @brief Insets belong to content bounds and produce exactly the authored trailing space.
TEST_F(scroll_fixture, BothAxesIncludeMarginsAndExcludeViewportPadding)
{
    tree.set_panel(viewport, scroll_layout{true, true, {1000, 1000}});
    auto style = tree.layout(viewport);
    style.padding = {5, 7, 11, 13};
    tree.set_layout(viewport, style);
    style = tree.layout(content);
    style.intrinsic = {200, 300};
    style.margin = {2, 3, 4, 6};
    tree.set_layout(content, style);
    const auto result = calculate();
    const auto scroll = scrolling(result);
    EXPECT_EQ(scroll.viewport, (layout_rect{5, 7, 84, 60}));
    EXPECT_EQ(scroll.extent, (layout_extent{206, 309}));
    EXPECT_EQ(scroll.offset, (layout_point{122, 249}));
    EXPECT_EQ(require_geometry(result.find(content)).bounds, (layout_rect{7, 10, 200, 300}));
}

/// @brief Empty and shorter content stays at its leading edge; zero extents stay finite.
TEST_F(scroll_fixture, EmptyShortAndZeroViewportHaveFiniteRanges)
{
    resize_content({20, 30});
    offset({0, 100});
    EXPECT_EQ(scrolling(calculate()).range, (layout_point{}));
    EXPECT_EQ(require_geometry(calculate().find(content)).bounds.y, 0);
    tree.set_viewport({0, 0, 0, 0});
    EXPECT_EQ(scrolling(calculate()).range.y, 30);
    tree.remove(content);
    EXPECT_EQ(scrolling(calculate()).extent, (layout_extent{}));
    EXPECT_EQ(scrolling(calculate()).offset, (layout_point{}));
}

/// @brief Invalid state and illegal child capacity fail without changing the live revision.
TEST_F(scroll_fixture, RejectsInvalidOffsetsAndMultipleContentsAtomically)
{
    const auto before = tree.snapshot();
    EXPECT_THROW(offset({0, std::numeric_limits<float>::infinity()}), std::invalid_argument);
    EXPECT_THROW(offset({std::numeric_limits<float>::quiet_NaN(), 0}), std::invalid_argument);
    EXPECT_THROW(tree.create(viewport), std::invalid_argument);
    EXPECT_EQ(tree.snapshot().revision(), before.revision());
    EXPECT_EQ(tree.size(), 2U);
}
} // namespace atlas::ui::tests
