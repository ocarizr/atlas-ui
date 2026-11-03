/** @file @brief Sibling boundaries resolve after natural measurement and before descendants. */
#include "layout_test_support.hpp"
#include <atlas/jobs/job_system.hpp>
#include <atlas/ui/element_tree.hpp>
#include <atlas/ui/layout_engine.hpp>
#include <gtest/gtest.h>

namespace atlas::ui::tests
{
/// @brief Independent facts own the complete shared dispatcher lifetime.
class boundary_geometry_test : public ::testing::Test
{
  protected:
    jobs::job_system pool{2};                ///< Reused by both numeric phases.
    element_tree tree;                       ///< Owner-affine authoring state.
    layout_engine engine{pool.dispatcher()}; ///< No renderer or private pool.

    element_handle canvas()
    {
        const auto result = tree.create();
        tree.set_panel(result, canvas_layout{});
        return result;
    } ///< Create a noncircular layout domain.
};

/// @brief Reverse insertion order cannot make a dependent consume unfinished source geometry.
TEST_F(boundary_geometry_test, content_list_defines_remaining_panel_and_resizes)
{
    const auto root = canvas();
    const auto panel = tree.create(root);
    const auto list = tree.create(root);
    tree.set_panel(list, vertical_layout{2});
    for (int index = 0; index < 2; ++index)
    {
        const auto child = tree.create(list);
        layout_style style;
        style.intrinsic = {20, 10};
        tree.set_layout(child, style);
    }

    tree.set_boundaries(
        panel, {{}, boundary_relation{list, boundary_side::after, 3, boundary_sizing::remaining}});
    const auto first = engine.update(tree, {0, 0, 100, 100});
    EXPECT_EQ(require_geometry(first.find(panel)).bounds, (layout_rect{0, 25, 0, 75}));
    const auto second = engine.update(tree, {10, 20, 200, 80});
    EXPECT_EQ(require_geometry(second.find(panel)).bounds, (layout_rect{10, 45, 0, 55}));
    EXPECT_EQ(tree.first_child(root), panel);
    EXPECT_EQ(tree.next_sibling(panel), list);
}

/// @brief Both axes can depend on different siblings; nested children see the final rectangle.
TEST_F(boundary_geometry_test, two_axes_and_nested_content_use_resolved_bounds)
{
    const auto root = canvas();
    const auto target = tree.create(root);
    const auto source = tree.create(root);
    layout_style style;
    style.intrinsic = {20, 30};
    tree.set_layout(source, style);
    const auto leaf = tree.create(target);
    style.intrinsic = {};
    style.horizontal = style.vertical = layout_alignment::stretch;
    tree.set_layout(leaf, style);
    tree.set_boundaries(
        target, {boundary_relation{source, boundary_side::after, 2, boundary_sizing::remaining},
                 boundary_relation{source, boundary_side::after, 3, boundary_sizing::remaining}});
    const auto result = engine.update(tree, {0, 0, 100, 100});
    const layout_rect expected{22, 33, 78, 67};
    EXPECT_EQ(require_geometry(result.find(target)).bounds, expected);
    EXPECT_EQ(require_geometry(result.find(leaf)).bounds, expected);
}
} // namespace atlas::ui::tests
