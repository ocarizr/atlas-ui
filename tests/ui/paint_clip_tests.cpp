/** @file @brief Inherited masks, opacity and translated oversized scroll content. */
#include "paint_test_support.hpp"
#include <limits>

namespace atlas::ui::tests
{
/// @brief Verify nested viewport clips translated oversized content.
TEST_F(paint_fixture, NestedViewportClipsTranslatedOversizedContent)
{
    const auto root = tree.create();
    layout_style size;
    size.intrinsic = {40, 30};
    size.constraints.minimum = {40, 30};
    size.constraints.maximum = {40, 30};
    size.horizontal = layout_alignment::start;
    size.vertical = layout_alignment::start;
    tree.set_layout(root, size);
    paint_style viewport;
    viewport.clip = true;
    viewport.translation = {10, 5};
    tree.set_paint(root, viewport);
    paint_style content;
    content.translation = {-20, -10};
    content.texture = 42;
    const auto child = rectangle(root, content);
    size.intrinsic = {100, 100};
    size.constraints.minimum = {100, 100};
    size.constraints.maximum = {100, 100};
    tree.set_layout(child, size);
    const auto output = capture();
    ASSERT_EQ(output.quads().size(), 1);
    EXPECT_EQ(output.quads()[0].bounds, (layout_rect{-10, -5, 100, 100}));
    EXPECT_EQ(output.quads()[0].clip, (layout_rect{10, 5, 40, 30}));
    EXPECT_EQ(output.quads()[0].texture, 42);
    EXPECT_EQ(output.quads()[0].vertices[5].uv, (layout_point{1, 1}));
}

/// @brief Verify fully outside clip omits draw but unclipped parent does not cull children.
TEST_F(paint_fixture, FullyOutsideClipOmitsDrawButUnclippedParentDoesNotCullChildren)
{
    const auto root = rectangle();
    auto style = tree.paint(root);
    style.translation = {-200, 0};
    tree.set_paint(root, style);
    const auto child = rectangle(root);
    style.translation = {200, 0};
    tree.set_paint(child, style);
    auto output = capture();
    ASSERT_EQ(output.quads().size(), 1);
    EXPECT_EQ(output.quads()[0].target, child);
    style = tree.paint(root);
    style.clip = true;
    tree.set_paint(root, style);
    EXPECT_TRUE(capture().quads().empty());
}

/// @brief Verify parent opacity multiplies alpha and visibility suppresses descendants.
TEST_F(paint_fixture, ParentOpacityMultipliesAlphaAndVisibilitySuppressesDescendants)
{
    const auto root = tree.create();
    const auto child = rectangle(root);
    tree.commands().begin_frame(1);
    tree.commands().set(root, property::opacity, 0.5f, 1);
    tree.commands().set(child, property::opacity, 0.5f, 1);
    tree.apply(tree.commands().seal());
    auto output = capture();
    ASSERT_EQ(output.quads().size(), 1);
    EXPECT_FLOAT_EQ(output.quads()[0].vertices[0].color[3], 0.25f);
    tree.commands().begin_frame(2);
    tree.commands().set(root, property::visible, false, 1);
    tree.apply(tree.commands().seal());
    EXPECT_TRUE(capture().quads().empty());
}

/// @brief Verify paint changes keep geometry revision and captured appearance immutable.
TEST_F(paint_fixture, PaintChangesKeepGeometryRevisionAndCapturedAppearanceImmutable)
{
    const auto node = rectangle();
    const auto before = capture();
    const auto source = tree.snapshot();
    const auto geometry = layout.calculate(source);
    auto style = tree.paint(node);
    style.color = {1, 0, 0, 0.5f};
    tree.set_paint(node, style);
    EXPECT_EQ(tree.snapshot().revision(), source.revision());
    const auto after = paint.calculate(tree.snapshot(), geometry);
    EXPECT_EQ(before.quads()[0].vertices[0].color, (std::array<float, 4>{1, 1, 1, 1}));
    EXPECT_EQ(after.quads()[0].vertices[0].color, style.color);
}

/// @brief Verify invalid paint is rejected without mutation.
TEST_F(paint_fixture, InvalidPaintIsRejectedWithoutMutation)
{
    const auto node = rectangle();
    const auto before = tree.paint(node);
    auto invalid = before;
    invalid.color[0] = std::numeric_limits<float>::quiet_NaN();
    EXPECT_THROW(tree.set_paint(node, invalid), std::invalid_argument);
    EXPECT_EQ(tree.paint(node), before);
    invalid = before;
    invalid.uv = {0.8f, 0, 0.5f, 1};
    EXPECT_THROW(tree.set_paint(node, invalid), std::invalid_argument);
    EXPECT_EQ(tree.paint(node), before);
}

/// @brief Verify empty and invalid intersections are explicit.
TEST(PaintClips, EmptyAndInvalidIntersectionsAreExplicit)
{
    EXPECT_EQ(intersect_clip({0, 0, 10, 10}, {20, 0, 10, 10}).width, 0);
    EXPECT_THROW(intersect_clip({0, 0, -1, 10}, {}), std::invalid_argument);
}
} // namespace atlas::ui::tests
