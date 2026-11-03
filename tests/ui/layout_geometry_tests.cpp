/** @file @brief Table-driven geometry and nested container contracts. */
#include "layout_test_support.hpp"
#include <atlas/jobs/job_system.hpp>
#include <atlas/ui/element_tree.hpp>
#include <atlas/ui/layout_engine.hpp>
#include <filesystem>
#include <gtest/gtest.h>
#include <pugixml.hpp>

namespace atlas::ui::tests
{
/// @brief Small fixture vocabulary is independent from the future authored-UI XML schema.
layout_kind kind(std::string_view name)
{
    if (name == "horizontal")
    {
        return layout_kind::horizontal;
    }

    if (name == "vertical")
    {
        return layout_kind::vertical;
    }

    if (name == "size_box")
    {
        return layout_kind::size_box;
    }

    if (name == "leaf")
    {
        return layout_kind::leaf;
    }

    return layout_kind::overlay;
}

/// @brief Missing alignment means stretch, matching the public default.
layout_alignment alignment(std::string_view name)
{
    if (name == "center")
    {
        return layout_alignment::center;
    }

    if (name == "end")
    {
        return layout_alignment::end;
    }

    if (name == "start")
    {
        return layout_alignment::start;
    }

    return layout_alignment::stretch;
}

/// @brief Retain expected XML nodes only while the local fixture document exists.
struct expected_node
{
    element_handle target; ///< Live tree identity.
    pugi::xml_node source; ///< Borrowed per-test document data.
};

/// @brief Recursion is limited to this tiny authored fixture; production layout is iterative.
void populate(element_tree& tree, pugi::xml_node xml, element_handle parent,
              std::vector<expected_node>& expected)
{
    const auto target = tree.create(parent);
    layout_style style;
    style.kind = kind(xml.attribute("kind").as_string());
    style.intrinsic = {xml.attribute("width").as_float(), xml.attribute("height").as_float()};
    style.horizontal = alignment(xml.attribute("horizontal").as_string());
    style.vertical = alignment(xml.attribute("vertical").as_string());
    style.padding = {xml.attribute("left").as_float(), xml.attribute("top").as_float(),
                     xml.attribute("right").as_float(), xml.attribute("bottom").as_float()};
    style.spacing = xml.attribute("spacing").as_float();
    tree.set_layout(target, style);
    expected.push_back({target, xml});
    for (auto child : xml.children("element"))
    {
        populate(tree, child, target, expected);
    }
}

/// @brief Each geometry scenario owns its pool, XML document, tree and engine.
class layout_geometry_test : public ::testing::TestWithParam<const char*>
{
};

/// @brief Golden rectangles cover stacking, padding, alignment, nesting and zero extents.
TEST_P(layout_geometry_test, matches_authored_geometry)
{
    pugi::xml_document document;
    const auto path = std::filesystem::path{ATLAS_UI_DATA_DIR} / "layout_cases.xml";
    ASSERT_TRUE(document.load_file(path.c_str()));
    const auto scenario =
        document.child("cases").find_child_by_attribute("case", "name", GetParam());
    ASSERT_TRUE(scenario);
    jobs::job_system pool{2};
    element_tree tree;
    std::vector<expected_node> expected;
    populate(tree, scenario.child("element"), {}, expected);
    layout_engine engine{pool.dispatcher()};
    const auto result = engine.update(tree, {0, 0, scenario.attribute("width").as_float(),
                                             scenario.attribute("height").as_float()});
    ASSERT_EQ(result.items().size(), expected.size());
    for (const auto& node : expected)
    {
        SCOPED_TRACE(node.source.attribute("name").as_string());
        const auto geometry = require_geometry(result.find(node.target));
        EXPECT_FLOAT_EQ(geometry.bounds.x, node.source.attribute("x").as_float());
        EXPECT_FLOAT_EQ(geometry.bounds.y, node.source.attribute("y").as_float());
        EXPECT_FLOAT_EQ(geometry.bounds.width, node.source.attribute("actual_width").as_float());
        EXPECT_FLOAT_EQ(geometry.bounds.height, node.source.attribute("actual_height").as_float());
    }
}

INSTANTIATE_TEST_SUITE_P(containers, layout_geometry_test,
                         ::testing::Values("overlay_center", "horizontal", "vertical", "size_box",
                                           "nested", "zero"),
                         [](const ::testing::TestParamInfo<const char*>& info)
                         {
                             return std::string{info.param};
                         });
} // namespace atlas::ui::tests
