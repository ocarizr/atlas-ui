/** @file @brief Allocation preserves useful small extents across extreme finite input scales. */
#include "layout_test_support.hpp"
#include <atlas/jobs/job_system.hpp>
#include <atlas/ui/element_tree.hpp>
#include <atlas/ui/layout_engine.hpp>
#include <gtest/gtest.h>
#include <limits>

namespace atlas::ui::tests
{
/// @brief Shrink solves retained size directly rather than subtracting almost equal enormous
/// values.
TEST(stack_numeric, very_large_desired_sizes_retain_a_small_available_extent)
{
    jobs::job_system pool{1};
    element_tree tree;
    const auto root = tree.create();
    horizontal_layout row;
    row.overflow = stack_overflow::shrink;
    tree.set_panel(root, row);
    const auto first = tree.create(root);
    const auto second = tree.create(root);
    layout_style style;
    style.intrinsic.width = std::numeric_limits<float>::max() / 4;
    tree.set_layout(first, style);
    tree.set_layout(second, style);
    layout_engine engine{pool.dispatcher()};
    const auto result = engine.update(tree, {0, 0, 1, 10});
    EXPECT_EQ(require_geometry(result.find(first)).bounds.width, 0.5f);
    EXPECT_EQ(require_geometry(result.find(second)).bounds.width, 0.5f);
    EXPECT_EQ(require_geometry(result.find(second)).bounds.x, 0.5f);
}

/// @brief Removing a saturated huge weight must not erase the remaining small active weight.
TEST(stack_numeric, growth_redistributes_after_a_large_weight_hits_its_maximum)
{
    jobs::job_system pool{1};
    element_tree tree;
    const auto root = tree.create();
    horizontal_layout row;
    row.fill = stack_fill::weighted;
    tree.set_panel(root, row);
    const auto first = tree.create(root);
    const auto second = tree.create(root);
    layout_style style;
    style.constraints.maximum.width = 1;
    tree.set_layout(first, style);
    tree.set_stack_slot(first, {0, std::numeric_limits<float>::max(), 1});
    layout_engine engine{pool.dispatcher()};
    const auto result = engine.update(tree, {0, 0, 2, 10});
    EXPECT_EQ(require_geometry(result.find(first)).bounds.width, 1.f);
    EXPECT_EQ(require_geometry(result.find(second)).bounds.width, 1.f);
}

/// @brief The parent owns main-axis fill; cross-axis alignment retains the common style contract.
TEST(stack_numeric, fill_keeps_cross_axis_alignment_and_fixed_zero)
{
    jobs::job_system pool{1};
    element_tree tree;
    const auto root = tree.create();
    vertical_layout column;
    column.fill = stack_fill::weighted;
    tree.set_panel(root, column);
    const auto growing = tree.create(root);
    const auto fixed = tree.create(root);
    layout_style style;
    style.intrinsic = {10, 20};
    style.horizontal = layout_alignment::center;
    style.vertical = layout_alignment::end;
    tree.set_layout(growing, style);
    tree.commands().begin_frame(1);
    tree.commands().set(fixed, property::height, 0.f, 1);
    tree.apply(tree.commands().seal());
    layout_engine engine{pool.dispatcher()};
    const auto result = engine.update(tree, {0, 0, 100, 100});
    EXPECT_EQ(require_geometry(result.find(growing)).bounds, (layout_rect{45, 0, 10, 100}));
    EXPECT_EQ(require_geometry(result.find(fixed)).bounds.height, 0.f);
    EXPECT_EQ(require_geometry(result.find(fixed)).bounds.y, 100.f);
}
} // namespace atlas::ui::tests
