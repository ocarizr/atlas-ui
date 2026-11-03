/** @file @brief Constraints, arity, viewport boundaries and invalid input leave state unchanged. */
#include "layout_test_support.hpp"
#include <atlas/jobs/job_system.hpp>
#include <atlas/ui/element_tree.hpp>
#include <atlas/ui/layout_engine.hpp>
#include <gtest/gtest.h>
#include <limits>

namespace atlas::ui::tests
{
/// @brief Independent invalid values with stable human-readable case identities.
struct invalid_style_case
{
    const char* name;   ///< CTest case name.
    layout_style value; ///< Only this case's invalid requested parameters.
};

/// @brief Build isolated copies instead of sharing mutable parameter state between tests.
std::vector<invalid_style_case> invalid_styles()
{
    std::vector<invalid_style_case> cases;
    const auto add = [&](const char* name, auto change)
    {
        layout_style value;
        change(value);
        cases.push_back({name, value});
    };
    add("negative_intrinsic",
        [](auto& value)
        {
            value.intrinsic.width = -1;
        });
    add("nan_intrinsic",
        [](auto& value)
        {
            value.intrinsic.height = std::numeric_limits<float>::quiet_NaN();
        });
    add("negative_margin",
        [](auto& value)
        {
            value.margin.left = -1;
        });
    add("infinite_padding",
        [](auto& value)
        {
            value.padding.top = std::numeric_limits<float>::infinity();
        });
    add("negative_spacing",
        [](auto& value)
        {
            value.spacing = -1;
        });
    add("reversed_bounds",
        [](auto& value)
        {
            value.constraints.minimum.width = 2;
            value.constraints.maximum.width = 1;
        });
    add("infinite_minimum",
        [](auto& value)
        {
            value.constraints.minimum.height = std::numeric_limits<float>::infinity();
        });
    add("nan_maximum",
        [](auto& value)
        {
            value.constraints.maximum.height = std::numeric_limits<float>::quiet_NaN();
        });
    // NOLINTBEGIN(clang-analyzer-optin.core.EnumCastOutOfRange) -- Deliberate public-input
    // rejection.
    add("unknown_kind",
        [](auto& value)
        {
            value.kind = static_cast<layout_kind>(255);
        });
    add("unknown_alignment",
        [](auto& value)
        {
            value.horizontal = static_cast<layout_alignment>(255);
        });
    // NOLINTEND(clang-analyzer-optin.core.EnumCastOutOfRange)
    return cases;
}

/// @brief Each invalid-style case has its own registry and previous valid geometry.
class invalid_layout_style_test : public ::testing::TestWithParam<invalid_style_case>
{
};

/// @brief Invalid parameters neither invalidate computed values nor advance snapshot revision.
TEST_P(invalid_layout_style_test, rejects_without_mutation)
{
    jobs::job_system pool{1};
    element_tree tree;
    const auto root = tree.create();
    layout_engine engine{pool.dispatcher()};
    engine.update(tree, {0, 0, 100, 50});
    tree.acknowledge(root);
    const auto revision = tree.snapshot().revision();
    EXPECT_THROW(tree.set_layout(root, GetParam().value), std::invalid_argument);
    EXPECT_EQ(tree.layout(root), layout_style{});
    EXPECT_EQ(tree.snapshot().revision(), revision);
    EXPECT_TRUE(tree.computed(root));
    EXPECT_FALSE(tree.dirty(root).layout);
}

INSTANTIATE_TEST_SUITE_P(parameters, invalid_layout_style_test,
                         ::testing::ValuesIn(invalid_styles()),
                         [](const ::testing::TestParamInfo<invalid_style_case>& info)
                         {
                             return std::string{info.param.name};
                         });

/// @brief Margins, min/max and non-stretch alignment compose in logical units.
TEST(layout_constraints, margins_and_bounds_position_the_border)
{
    jobs::job_system pool{1};
    element_tree tree;
    const auto root = tree.create();
    const auto child = tree.create(root);
    layout_style style;
    style.kind = layout_kind::leaf;
    style.intrinsic = {100, 50};
    style.margin = {10, 5, 20, 15};
    style.constraints = {{20, 10}, {60, 30}};
    style.horizontal = layout_alignment::end;
    style.vertical = layout_alignment::center;
    tree.set_layout(child, style);
    layout_engine engine{pool.dispatcher()};
    const auto result = engine.update(tree, {0, 0, 200, 100});
    ASSERT_TRUE(result.find(child));
    EXPECT_EQ(require_geometry(result.find(child)).desired, (layout_extent{60, 30}));
    EXPECT_EQ(require_geometry(result.find(child)).bounds, (layout_rect{120, 30, 60, 30}));
}

/// @brief Minimum extents can overflow a slot; clipping is a later paint responsibility.
TEST(layout_constraints, minimum_overflow_keeps_finite_geometry)
{
    jobs::job_system pool{1};
    element_tree tree;
    const auto root = tree.create();
    const auto child = tree.create(root);
    layout_style style;
    style.constraints.minimum = {20, 10};
    style.horizontal = style.vertical = layout_alignment::end;
    tree.set_layout(child, style);
    layout_engine engine{pool.dispatcher()};
    const auto result = engine.update(tree, {0, 0, 5, 5});
    EXPECT_EQ(require_geometry(result.find(child)).bounds, (layout_rect{-15, -5, 20, 10}));
}

/// @brief Container arity is checked on conversion, creation and reparenting before topology edits.
TEST(layout_constraints, leaf_and_size_box_reject_extra_children)
{
    element_tree tree;
    const auto box = tree.create();
    layout_style style;
    style.kind = layout_kind::size_box;
    tree.set_layout(box, style);
    const auto first = tree.create(box);
    const auto other = tree.create();
    EXPECT_THROW(tree.create(box), std::invalid_argument);
    EXPECT_THROW(tree.reparent(other, box), std::invalid_argument);
    EXPECT_EQ(tree.parent(other), element_handle{});
    style.kind = layout_kind::leaf;
    EXPECT_THROW(tree.set_layout(box, style), std::invalid_argument);
    tree.set_layout(other, style);
    EXPECT_THROW(tree.create(other), std::invalid_argument);
    EXPECT_EQ(tree.first_child(box), first);
    EXPECT_EQ(tree.size(), 3U);
}

/// @brief Invalid viewports preserve the active input identity and computed result.
TEST(layout_constraints, rejects_invalid_viewport_without_partial_changes)
{
    element_tree tree;
    const auto root = tree.create();
    const auto revision = tree.snapshot().revision();
    EXPECT_THROW(tree.set_viewport({0, 0, -1, 10}), std::invalid_argument);
    EXPECT_THROW(tree.set_viewport(
                     {std::numeric_limits<float>::max(), 0, std::numeric_limits<float>::max(), 1}),
                 std::invalid_argument);
    EXPECT_EQ(tree.snapshot().revision(), revision);
    EXPECT_TRUE(tree.contains(root));
}
} // namespace atlas::ui::tests
