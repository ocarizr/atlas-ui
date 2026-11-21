/** @file @brief Shared scroll transforms and masks for ordered painting and input. */
#include "scroll_test_support.hpp"

namespace atlas::ui::tests
{
/// @brief The content moves, its viewport background stays still, and input follows paint.
TEST_F(scroll_fixture, PaintAndHitTestingShareScrollTranslationAndClip)
{
    offset({0, 50});
    const auto source = tree.snapshot();
    const auto geometry = layout.calculate(source);
    const auto result = paint.calculate(source, geometry);
    ASSERT_EQ(result.quads().size(), 1U);
    EXPECT_EQ(result.quads()[0].bounds, (layout_rect{0, -50, 100, 200}));
    EXPECT_EQ(result.quads()[0].clip, (layout_rect{0, 0, 100, 80}));
    input.bind(content, handler);
    input.begin_frame(1, source, geometry);
    EXPECT_EQ(input.hit_test({20, 20}), content);
    EXPECT_NE(input.hit_test({20, 90}), content);
    input.process({});
    finish();
}

/// @brief Nested scrolling intersects masks after the parent translation, including empty clips.
TEST_F(scroll_fixture, NestedViewportClipUsesTranslatedParentSpace)
{
    tree.set_panel(content, scroll_layout{false, true, {0, 25}});
    const auto leaf = rectangle(content);
    auto style = tree.layout(leaf);
    style.intrinsic = {100, 400};
    tree.set_layout(leaf, style);
    offset({0, 50});
    auto result = capture();
    ASSERT_EQ(result.quads().size(), 2U);
    EXPECT_EQ(result.quads()[1].bounds, (layout_rect{0, -75, 100, 400}));
    EXPECT_EQ(result.quads()[1].clip, (layout_rect{0, 0, 100, 80}));
    auto visual = tree.paint(content);
    visual.translation = {200, 0};
    tree.set_paint(content, visual);
    EXPECT_TRUE(capture().quads().empty());
}

/// @brief A scroll owner can draw its background without translating it with the children.
TEST_F(scroll_fixture, OwnerPaintIsNotScrolledAndOldRevisionCannotBeReused)
{
    auto style = tree.paint(viewport);
    style.fill = true;
    tree.set_paint(viewport, style);
    const auto old = calculate();
    offset({0, 120});
    EXPECT_THROW(paint.calculate(tree.snapshot(), old), std::invalid_argument);
    const auto result = capture();
    ASSERT_EQ(result.quads().size(), 2U);
    EXPECT_EQ(result.quads()[0].bounds, (layout_rect{0, 0, 100, 80}));
    EXPECT_EQ(result.quads()[1].bounds.y, -120);
}

/// @brief Reference pixel centers see the final list item at the bottom without blank overscroll.
TEST_F(scroll_fixture, ReferencePixelCoverageAtMaximumOffsetHasNoBlankOverscroll)
{
    tree.set_viewport({0, 0, 16, 16});
    tree.set_panel(content, vertical_layout{});
    tree.set_paint(content, {});
    resize_content({});
    for (unsigned index = 0; index < 3; ++index)
    {
        const auto child = rectangle(content);
        auto style = tree.layout(child);
        style.intrinsic = {16, 10};
        tree.set_layout(child, style);
    }

    offset({0, 1000});
    const auto source = tree.snapshot();
    const auto result = paint.calculate(source, layout.calculate(source));
    ASSERT_EQ(result.quads().size(), 2U);
    for (unsigned y = 0; y < 16; ++y)
    {
        for (unsigned x = 0; x < 16; ++x)
        {
            const auto inside = [x, y](layout_rect rect)
            {
                const auto px = static_cast<float>(x) + 0.5f;
                const auto py = static_cast<float>(y) + 0.5f;
                return px >= rect.x && px < rect.x + rect.width && py >= rect.y &&
                       py < rect.y + rect.height;
            };
            unsigned coverage = 0;
            for (const auto& quad : result.quads())
            {
                coverage += inside(quad.bounds) && inside(quad.clip) ? 1U : 0U;
            }

            EXPECT_EQ(coverage, 1U) << x << ',' << y;
        }
    }
}
} // namespace atlas::ui::tests
