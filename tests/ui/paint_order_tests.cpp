/** @file @brief Stacking contexts preserve subtree isolation and deterministic painter order. */
#include <atlas/jobs/job_system.hpp>
#include <atlas/ui/element_tree.hpp>
#include <atlas/ui/layout_engine.hpp>
#include <atlas/ui/paint_engine.hpp>
#include <gtest/gtest.h>

namespace atlas::ui::tests
{
class paint_test : public ::testing::Test
{
  protected:
    element_handle rectangle(element_handle parent = {}, std::int32_t layer = 0)
    {
        const auto node = tree.create(parent);
        paint_style style;
        style.fill = true;
        style.layer = layer;
        tree.set_paint(node, style);
        return node;
    } ///< Create one visible rectangle with a parent-local stacking layer.

    paint_snapshot capture()
    {
        tree.set_viewport({0, 0, 100, 80});
        const auto source = tree.snapshot();
        return paint.calculate(source, layout.calculate(source));
    } ///< Feed exactly matching immutable input and calculated geometry.

    jobs::job_system workers{2}; ///< One fixture-local pool, joined before tree destruction.
    element_tree tree;           ///< Owner-only authoring state.
    layout_engine layout{workers.dispatcher()}; ///< Sequential geometry reference.
    paint_engine paint{workers.dispatcher()};   ///< Deterministic paint coordinator.
}; ///< Isolated sessions and services for each fact.

/// @brief Verify smaller layer is foreground and children cannot escape their group.
TEST_F(paint_test, SmallerLayerIsForegroundAndChildrenCannotEscapeTheirGroup)
{
    const auto background = rectangle({}, 10);
    const auto child = rectangle(background, -100);
    const auto foreground = rectangle({}, 0);
    const auto result = capture();
    ASSERT_EQ(result.quads().size(), 3);
    EXPECT_EQ(result.quads()[0].target, background);
    EXPECT_EQ(result.quads()[1].target, child);
    EXPECT_EQ(result.quads()[2].target, foreground);
}

/// @brief Verify equal layers retain insertion order and parent background comes first.
TEST_F(paint_test, EqualLayersRetainInsertionOrderAndParentBackgroundComesFirst)
{
    const auto root = rectangle();
    const auto a = rectangle(root);
    const auto b = rectangle(root);
    const auto result = capture();
    ASSERT_EQ(result.quads().size(), 3);
    EXPECT_EQ(result.quads()[0].target, root);
    EXPECT_EQ(result.quads()[1].target, a);
    EXPECT_EQ(result.quads()[2].target, b);
}

/// @brief Explicit stack layout order changes geometry without changing equal-layer paint order.
TEST_F(paint_test, LayoutOrderAndPainterOrderRemainIndependent)
{
    const auto root = rectangle();
    tree.set_panel(root, vertical_layout{});
    const auto a = rectangle(root);
    const auto b = rectangle(root);
    layout_style size;
    size.constraints.minimum = {10, 10};
    tree.set_layout(a, size);
    tree.set_layout(b, size);
    tree.set_stack_slot(b, {-1});
    const auto result = capture();
    ASSERT_EQ(result.quads().size(), 3);
    EXPECT_EQ(result.quads()[1].target, a);
    EXPECT_EQ(result.quads()[2].target, b);
    EXPECT_GT(result.quads()[1].bounds.y, result.quads()[2].bounds.y);
}
} // namespace atlas::ui::tests
