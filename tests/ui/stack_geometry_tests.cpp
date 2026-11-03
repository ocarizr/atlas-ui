/** @file @brief XML reference geometry covers both stack orientations and bounded allocation. */
#include "layout_test_support.hpp"
#include <atlas/jobs/job_system.hpp>
#include <atlas/ui/element_tree.hpp>
#include <atlas/ui/layout_engine.hpp>
#include <filesystem>
#include <gtest/gtest.h>
#include <limits>
#include <pugixml.hpp>
#include <tuple>

namespace atlas::ui::tests
{
/// @brief Small fixture vocabulary maps data to the public enum, never to a separate solver.
stack_distribution distribution(std::string_view value)
{
    if (value == "center")
    {
        return stack_distribution::center;
    }

    if (value == "end")
    {
        return stack_distribution::end;
    }

    if (value == "space_between")
    {
        return stack_distribution::space_between;
    }

    if (value == "space_around")
    {
        return stack_distribution::space_around;
    }

    if (value == "space_evenly")
    {
        return stack_distribution::space_evenly;
    }

    return stack_distribution::start;
}

/// @brief Each parameter receives an isolated document, tree, pool and engine.
class stack_geometry_test : public ::testing::TestWithParam<std::tuple<const char*, bool>>
{
};

/// @brief All expected positions/sizes are authored independently from production arithmetic.
TEST_P(stack_geometry_test, matches_reference_main_axis_geometry)
{
    const auto [name, horizontal] = GetParam();
    pugi::xml_document document;
    ASSERT_TRUE(
        document.load_file((std::filesystem::path{ATLAS_UI_DATA_DIR} / "stack_cases.xml").c_str()));
    const auto xml = document.child("cases").find_child_by_attribute("case", "name", name);
    ASSERT_TRUE(xml);
    jobs::job_system pool{2};
    element_tree tree;
    const auto root = tree.create();
    horizontal_layout policy;
    policy.spacing = xml.attribute("spacing").as_float();
    policy.distribution = distribution(xml.attribute("distribution").as_string());
    policy.fill = xml.attribute("fill").as_bool() ? stack_fill::weighted : stack_fill::none;
    policy.overflow =
        xml.attribute("shrink").as_bool() ? stack_overflow::shrink : stack_overflow::allow;
    tree.set_panel(root, horizontal
                             ? panel_layout{policy}
                             : panel_layout{vertical_layout{policy.spacing, policy.distribution,
                                                            policy.fill, policy.overflow}});
    layout_style parent;
    if (xml.attribute("content").as_bool())
    {
        (horizontal ? parent.horizontal : parent.vertical) = layout_alignment::start;
    }

    tree.set_layout(root, parent);
    std::vector<std::pair<element_handle, pugi::xml_node>> expected;
    tree.commands().begin_frame(1);
    for (const auto child_xml : xml.children("child"))
    {
        const auto child = tree.create(root);
        layout_style style;
        style.intrinsic = horizontal ? layout_extent{child_xml.attribute("size").as_float(), 10}
                                     : layout_extent{10, child_xml.attribute("size").as_float()};
        (horizontal ? style.constraints.minimum.width : style.constraints.minimum.height) =
            child_xml.attribute("minimum").as_float();
        (horizontal ? style.constraints.maximum.width : style.constraints.maximum.height) =
            child_xml.attribute("maximum").as_float(std::numeric_limits<float>::infinity());
        (horizontal ? style.margin.left : style.margin.top) =
            child_xml.attribute("before").as_float();
        (horizontal ? style.margin.right : style.margin.bottom) =
            child_xml.attribute("after").as_float();
        tree.set_layout(child, style);
        tree.set_stack_slot(child, {0, child_xml.attribute("grow").as_float(1),
                                    child_xml.attribute("shrink").as_float(1)});
        if (child_xml.attribute("fixed").as_bool())
        {
            tree.commands().set(child, horizontal ? property::width : property::height,
                                child_xml.attribute("size").as_float(), 1);
        }

        expected.emplace_back(child, child_xml);
    }

    tree.apply(tree.commands().seal());
    const auto extent = xml.attribute("extent").as_float(100);
    layout_engine engine{pool.dispatcher()};
    const auto result = engine.update(tree, horizontal ? layout_rect{0, 0, extent, 80}
                                                       : layout_rect{0, 0, 80, extent});
    ASSERT_EQ(result.items().size(), expected.size() + 1);
    const auto root_bounds = require_geometry(result.find(root)).bounds;
    EXPECT_FLOAT_EQ(horizontal ? root_bounds.width : root_bounds.height,
                    xml.attribute("actual_root").as_float(extent));
    for (const auto& [child, source] : expected)
    {
        const auto bounds = require_geometry(result.find(child)).bounds;
        EXPECT_FLOAT_EQ(horizontal ? bounds.x : bounds.y, source.attribute("position").as_float());
        EXPECT_FLOAT_EQ(horizontal ? bounds.width : bounds.height,
                        source.attribute("actual").as_float());
    }
}

INSTANTIATE_TEST_SUITE_P(
    policies, stack_geometry_test,
    ::testing::Combine(::testing::Values("start", "center", "end", "between", "around", "evenly",
                                         "equal_fill", "weighted_fill", "capped_fill",
                                         "fill_opt_out", "weighted_shrink", "capped_shrink",
                                         "allowed_overflow", "minimum_overflow", "zero_extent",
                                         "single_evenly", "empty", "margins", "content_sized",
                                         "fixed_shrink"),
                       ::testing::Bool()),
    [](const ::testing::TestParamInfo<std::tuple<const char*, bool>>& value)
    {
        return std::string{std::get<0>(value.param)} +
               (std::get<1>(value.param) ? "_horizontal" : "_vertical");
    });
} // namespace atlas::ui::tests
