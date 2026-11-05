/** @file @brief Image aspect fitting preserves logical bounds and normalized UV regions. */
#include "paint_test_support.hpp"
#include <atlas/ui/image_fit.hpp>
#include <gtest/gtest.h>

/// @brief Contain centers the source without stretching or modifying its UV region.
TEST(ImageFit, ContainsWideImageInSquare)
{
    const auto result =
        atlas::ui::fit_image({0, 0, 100, 100}, {{200, 100}, atlas::ui::image_fit::contain});
    EXPECT_EQ(result.bounds, (atlas::ui::layout_rect{0, 25, 100, 50}));
    EXPECT_EQ(result.uv, (atlas::ui::layout_rect{0, 0, 1, 1}));
}

/// @brief Cover fills the border while cropping the source region symmetrically.
TEST(ImageFit, CoversSquareWithWideImage)
{
    const auto result =
        atlas::ui::fit_image({0, 0, 100, 100}, {{200, 100}, atlas::ui::image_fit::cover});
    EXPECT_EQ(result.bounds, (atlas::ui::layout_rect{0, 0, 100, 100}));
    EXPECT_EQ(result.uv, (atlas::ui::layout_rect{0.25f, 0, 0.5f, 1}));
}

/// @brief Aspect fitting uses the selected atlas region rather than unrelated source pixels.
TEST(ImageFit, SelectedSquareAtlasRegionHasSquareAspect)
{
    const auto result = atlas::ui::fit_image(
        {0, 0, 100, 100}, {{200, 100}, atlas::ui::image_fit::contain}, {0.5f, 0, 0.5f, 1});
    EXPECT_EQ(result.bounds, (atlas::ui::layout_rect{0, 0, 100, 100}));
    EXPECT_EQ(result.uv, (atlas::ui::layout_rect{0.5f, 0, 0.5f, 1}));
}

/// @brief Empty destinations and atlas regions remain well-defined without division by zero.
TEST(ImageFit, EmptyAndStretchInputsPreserveExpectedGeometry)
{
    using namespace atlas::ui;
    EXPECT_EQ(fit_image({2, 3, 0, 20}, {{10, 10}, image_fit::contain}).bounds,
              (layout_rect{2, 3, 0, 20}));
    EXPECT_EQ(fit_image({2, 3, 20, 20}, {{10, 10}, image_fit::stretch}).bounds,
              (layout_rect{2, 3, 20, 20}));
    const auto empty = fit_image({2, 3, 20, 20}, {{10, 10}, image_fit::cover}, {0, 0, 0, 1});
    EXPECT_EQ(empty.bounds.width, 0);
    EXPECT_EQ(empty.bounds.height, 0);
}

/// @brief Invalid source/UV and unrepresentable trailing edges fail before producing output.
TEST(ImageFit, RejectsInvalidDimensionsAndOverflow)
{
    using namespace atlas::ui;
    EXPECT_THROW(fit_image({0, 0, 10, 10}, {{0, 10}, image_fit::contain}), std::invalid_argument);
    EXPECT_THROW(fit_image({0, 0, 10, 10}, {{10, 10}, image_fit::cover}, {0.8f, 0, 0.5f, 1}),
                 std::invalid_argument);
    const auto maximum = std::numeric_limits<float>::max();
    EXPECT_THROW(fit_image({maximum, 0, maximum, 10}, {{10, 10}, image_fit::contain}),
                 std::overflow_error);
}

namespace atlas::ui::tests
{
/// @brief Fitting changes the painted border while layout and clipping retain their contracts.
TEST_F(paint_fixture, ImageFittingPreservesGeometryAndPaintImmutability)
{
    paint_style style;
    style.texture = 42;
    style.image = image_sampling{{200, 80}, image_fit::contain};
    const auto node = rectangle({}, style);
    const auto before = capture();
    ASSERT_EQ(before.quads().size(), 1);
    EXPECT_EQ(before.quads()[0].bounds, (layout_rect{0, 20, 100, 40}));
    EXPECT_EQ(before.quads()[0].clip, (layout_rect{0, 0, 100, 80}));
    const auto input = tree.snapshot();
    const auto geometry = layout.calculate(input);
    style.fill = true;
    style.image = image_sampling{{200, 80}, image_fit::cover};
    tree.set_paint(node, style);
    const auto after = paint.calculate(tree.snapshot(), geometry);
    EXPECT_EQ(after.quads()[0].bounds, (layout_rect{0, 0, 100, 80}));
    EXPECT_EQ(before.quads()[0].bounds, (layout_rect{0, 20, 100, 40}));
    style.image = image_sampling{{0, 80}, image_fit::contain};
    EXPECT_THROW(tree.set_paint(node, style), std::invalid_argument);
    const auto current = tree.paint(node);
    if (current.image)
    {
        EXPECT_EQ(current.image->source.width, 200);
    }
    else
    {
        FAIL() << "Rejected image fitting removed the previous valid appearance";
    }

    style.image.reset();
    style.texture = 0;
    tree.set_paint(node, style);
    const auto solid = paint.calculate(tree.snapshot(), geometry);
    EXPECT_EQ(solid.quads()[0].bounds, (layout_rect{0, 0, 100, 80}));
}
} // namespace atlas::ui::tests
