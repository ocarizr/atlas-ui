/** @file @brief Data-driven before/after allocation, margins, constraints and overflow on both
 * axes. */
#include "layout_test_support.hpp"
#include <atlas/jobs/job_system.hpp>
#include <atlas/ui/element_tree.hpp>
#include <atlas/ui/layout_engine.hpp>
#include <filesystem>
#include <gtest/gtest.h>
#include <pugixml.hpp>
#include <tuple>

namespace atlas::ui::tests
{
/// @brief Every XML parameter owns independent mutable state and allocator lifetimes.
class boundary_axis_test : public ::testing::TestWithParam<std::tuple<const char*, bool>>
{
};

/// @brief Reference numbers are authored separately from the runtime implementation.
TEST_P(boundary_axis_test, matches_authored_geometry_or_rejects_incompatible_bounds)
{
    const auto [name, horizontal] = GetParam();
    pugi::xml_document data;
    ASSERT_TRUE(
        data.load_file((std::filesystem::path{ATLAS_UI_DATA_DIR} / "boundary_cases.xml").c_str()));
    const auto xml = data.child("cases").find_child_by_attribute("case", "name", name);
    ASSERT_TRUE(xml);
    jobs::job_system pool{1};
    element_tree tree;
    layout_engine engine{pool.dispatcher()};
    const auto root = tree.create();
    tree.set_panel(root, canvas_layout{});
    layout_style parent;
    (horizontal ? parent.padding.left : parent.padding.top) =
        xml.attribute("padding_before").as_float();
    (horizontal ? parent.padding.right : parent.padding.bottom) =
        xml.attribute("padding_after").as_float();
    tree.set_layout(root, parent);
    const auto target = tree.create(root);
    const auto source = tree.create(root);
    layout_style source_style;
    (horizontal ? source_style.intrinsic.width : source_style.intrinsic.height) =
        xml.attribute("source_size").as_float(20);
    (horizontal ? source_style.margin.left : source_style.margin.top) =
        xml.attribute("source_before").as_float();
    (horizontal ? source_style.margin.right : source_style.margin.bottom) =
        xml.attribute("source_after").as_float();
    tree.set_layout(source, source_style);
    canvas_slot source_slot;
    (horizontal ? source_slot.horizontal : source_slot.vertical).offset_min =
        xml.attribute("source_offset").as_float();
    tree.set_canvas_slot(source, source_slot);
    layout_style style;
    (horizontal ? style.intrinsic.width : style.intrinsic.height) = 10;
    (horizontal ? style.margin.left : style.margin.top) = xml.attribute("target_before").as_float();
    (horizontal ? style.margin.right : style.margin.bottom) =
        xml.attribute("target_after").as_float();
    (horizontal ? style.constraints.minimum.width : style.constraints.minimum.height) =
        xml.attribute("minimum").as_float();
    (horizontal ? style.constraints.maximum.width : style.constraints.maximum.height) =
        xml.attribute("maximum").as_float(std::numeric_limits<float>::infinity());
    tree.set_layout(target, style);
    if (xml.attribute("fixed"))
    {
        tree.commands().begin_frame(1);
        tree.commands().set(target, horizontal ? property::width : property::height,
                            xml.attribute("fixed").as_float(), 1);
        tree.apply(tree.commands().seal());
    }

    const boundary_relation relation{
        source, xml.attribute("before").as_bool() ? boundary_side::before : boundary_side::after,
        xml.attribute("gap").as_float(),
        xml.attribute("remaining").as_bool() ? boundary_sizing::remaining
                                             : boundary_sizing::desired,
        xml.attribute("reject").as_bool() ? boundary_overflow::reject : boundary_overflow::allow};
    layout_boundaries boundaries;
    (horizontal ? boundaries.horizontal : boundaries.vertical) = relation;
    tree.set_boundaries(target, boundaries);
    if (xml.attribute("failure").as_bool())
    {
        EXPECT_THROW(engine.update(tree, {10, 10, 100, 100}), std::invalid_argument);
        EXPECT_FALSE(tree.computed(target));
        EXPECT_FALSE(tree.computed(source));
        return;
    }

    const auto result = engine.update(tree, {10, 10, 100, 100});
    const auto bounds = require_geometry(result.find(target)).bounds;
    EXPECT_FLOAT_EQ(horizontal ? bounds.x : bounds.y, 10 + xml.attribute("position").as_float());
    EXPECT_FLOAT_EQ(horizontal ? bounds.width : bounds.height, xml.attribute("size").as_float());
}

INSTANTIATE_TEST_SUITE_P(
    both_axes, boundary_axis_test,
    ::testing::Combine(::testing::Values("after_desired", "before_desired", "after_remaining",
                                         "before_remaining", "maximum_caps_remaining",
                                         "minimum_overflows", "minimum_rejected",
                                         "desired_overflows", "desired_rejected", "fixed_remaining",
                                         "fixed_zero", "outer_margins_after",
                                         "outer_margins_before", "parent_padding", "source_outside",
                                         "source_outside_rejected", "before_negative",
                                         "before_negative_rejected", "exact_fit", "zero_available"),
                       ::testing::Bool()),
    [](const auto& info)
    {
        return std::string{std::get<0>(info.param)} +
               (std::get<1>(info.param) ? "_horizontal" : "_vertical");
    });
} // namespace atlas::ui::tests
