/** @file @brief Authored numeric reference cases and noncircular nested-canvas sizing. */
#include "layout_test_support.hpp"
#include <atlas/jobs/job_system.hpp>
#include <atlas/ui/element_tree.hpp>
#include <atlas/ui/layout_engine.hpp>
#include <filesystem>
#include <gtest/gtest.h>
#include <pugixml.hpp>

namespace atlas::ui::tests
{
/// @brief Each parameter loads a separate document and owns its execution resources.
class canvas_geometry_test : public ::testing::TestWithParam<const char*>
{
};

/// @brief Anchor intervals use parent content extents; margins and constraints apply afterwards.
TEST_P(canvas_geometry_test, matches_reference_border)
{
    pugi::xml_document document;
    ASSERT_TRUE(document.load_file(
        (std::filesystem::path{ATLAS_UI_DATA_DIR} / "canvas_cases.xml").c_str()));
    const auto xml = document.child("cases").find_child_by_attribute("case", "name", GetParam());
    ASSERT_TRUE(xml);
    jobs::job_system pool{2};
    element_tree tree;
    const auto root = tree.create();
    tree.set_panel(root, canvas_layout{});
    layout_style parent;
    parent.padding = {xml.attribute("left").as_float(), xml.attribute("top").as_float(),
                      xml.attribute("right").as_float(), xml.attribute("bottom").as_float()};
    tree.set_layout(root, parent);
    const auto child = tree.create(root);
    layout_style style;
    style.intrinsic = {20, 10};
    style.margin = {xml.attribute("margin_left").as_float(), xml.attribute("margin_top").as_float(),
                    xml.attribute("margin_right").as_float(),
                    xml.attribute("margin_bottom").as_float()};
    style.constraints.minimum = {xml.attribute("minimum_width").as_float(),
                                 xml.attribute("minimum_height").as_float()};
    tree.set_layout(child, style);
    canvas_slot slot;
    slot.horizontal = {xml.attribute("min_x").as_float(), xml.attribute("max_x").as_float(),
                       xml.attribute("offset_x").as_float(), xml.attribute("end_x").as_float(),
                       xml.attribute("pivot_x").as_float()};
    slot.vertical = {xml.attribute("min_y").as_float(), xml.attribute("max_y").as_float(),
                     xml.attribute("offset_y").as_float(), xml.attribute("end_y").as_float(),
                     xml.attribute("pivot_y").as_float()};
    if (xml.attribute("stretch").as_bool())
    {
        slot.horizontal.sizing = slot.vertical.sizing = canvas_sizing::stretch;
    }

    tree.set_canvas_slot(child, slot);
    if (xml.attribute("fixed").as_bool())
    {
        tree.commands().begin_frame(1);
        tree.commands().set(child, property::width, 23.f, 1);
        tree.commands().set(child, property::height, 11.f, 1);
        tree.apply(tree.commands().seal());
    }

    layout_engine engine{pool.dispatcher()};
    const auto geometry =
        require_geometry(engine
                             .update(tree, {0, 0, xml.attribute("width").as_float(),
                                            xml.attribute("height").as_float()})
                             .find(child));
    EXPECT_FLOAT_EQ(geometry.bounds.x, xml.attribute("x").as_float());
    EXPECT_FLOAT_EQ(geometry.bounds.y, xml.attribute("y").as_float());
    EXPECT_FLOAT_EQ(geometry.bounds.width, xml.attribute("actual_width").as_float());
    EXPECT_FLOAT_EQ(geometry.bounds.height, xml.attribute("actual_height").as_float());
}

INSTANTIATE_TEST_SUITE_P(placement, canvas_geometry_test,
                         ::testing::Values("top_left", "bottom_right", "center",
                                           "padding_and_margins", "signed_offsets",
                                           "anchor_interval", "fixed_overrides_stretch",
                                           "minimum_overflow", "zero_stretch"),
                         [](const ::testing::TestParamInfo<const char*>& value)
                         {
                             return std::string{value.param};
                         });

/// @brief Content-driven canvas sizing uses only absolute slots and includes padding/margins.
TEST(canvas_geometry, absolute_children_measure_without_parent_size_equations)
{
    jobs::job_system pool{1};
    element_tree tree;
    const auto root = tree.create();
    tree.set_panel(root, canvas_layout{canvas_measure::absolute_children});
    layout_style parent;
    parent.horizontal = parent.vertical = layout_alignment::start;
    parent.padding = {1, 2, 3, 4};
    tree.set_layout(root, parent);
    const auto child = tree.create(root);
    layout_style style;
    style.intrinsic = {20, 10};
    style.margin = {2, 3, 4, 5};
    tree.set_layout(child, style);
    canvas_slot slot;
    slot.horizontal.offset_min = 7;
    slot.vertical.offset_min = 9;
    tree.set_canvas_slot(child, slot);
    layout_engine engine{pool.dispatcher()};
    const auto result = engine.update(tree, {0, 0, 200, 100});
    EXPECT_EQ(require_geometry(result.find(root)).desired, (layout_extent{37, 33}));
    EXPECT_EQ(require_geometry(result.find(root)).bounds, (layout_rect{0, 0, 37, 33}));
    EXPECT_EQ(require_geometry(result.find(child)).bounds, (layout_rect{10, 14, 20, 10}));
}

/// @brief Nested canvas children resolve against the arranged parent's content, never the screen.
TEST(canvas_geometry, nested_canvases_use_resolved_local_content)
{
    jobs::job_system pool{1};
    element_tree tree;
    const auto root = tree.create();
    tree.set_panel(root, canvas_layout{});
    const auto inner = tree.create(root);
    tree.set_panel(inner, canvas_layout{});
    canvas_slot area;
    area.horizontal = area.vertical = {0.5f, 1, 0, 0, 0, canvas_sizing::stretch};
    tree.set_canvas_slot(inner, area);
    const auto leaf = tree.create(inner);
    layout_style style;
    style.intrinsic = {20, 10};
    tree.set_layout(leaf, style);
    canvas_slot corner;
    corner.horizontal = corner.vertical = {1, 1, 0, 0, 1};
    tree.set_canvas_slot(leaf, corner);
    layout_engine engine{pool.dispatcher()};
    const auto result = engine.update(tree, {0, 0, 200, 100});
    EXPECT_EQ(require_geometry(result.find(inner)).bounds, (layout_rect{100, 50, 100, 50}));
    EXPECT_EQ(require_geometry(result.find(leaf)).bounds, (layout_rect{180, 90, 20, 10}));
}
} // namespace atlas::ui::tests
