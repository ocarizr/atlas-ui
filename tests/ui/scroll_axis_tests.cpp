/** @file @brief Reproducible XML reference tables and finite axis sizing policies. */
#include "scroll_test_support.hpp"
#include <filesystem>
#include <pugixml.hpp>

namespace atlas::ui::tests
{
/// @brief Per-case XML documents and trees are never shared between test instances.
class scroll_reference_test : public scroll_fixture, public testing::WithParamInterface<const char*>
{
};

/// @brief Both endpoints and middle offsets preserve the exact finite reference range.
TEST_P(scroll_reference_test, MatchesReferenceGeometry)
{
    pugi::xml_document data;
    ASSERT_TRUE(
        data.load_file((std::filesystem::path{ATLAS_UI_DATA_DIR} / "scroll_geometry.xml").c_str()));
    const auto name = std::string{GetParam()};
    const auto row =
        data.child("scroll-cases").find_child_by_attribute("case", "name", name.c_str());
    ASSERT_TRUE(row);
    resize_content({row.attribute("width").as_float(), row.attribute("height").as_float()});
    const layout_point requested{row.attribute("x").as_float(), row.attribute("y").as_float()};
    tree.set_panel(viewport, scroll_layout{true, true, requested});
    const auto result = scrolling(calculate());
    const layout_point range{row.attribute("range-x").as_float(),
                             row.attribute("range-y").as_float()};
    EXPECT_EQ(result.range, range);
    EXPECT_EQ(result.offset, (layout_point{std::clamp(requested.x, 0.f, range.x),
                                           std::clamp(requested.y, 0.f, range.y)}));
}

INSTANTIATE_TEST_SUITE_P(reference, scroll_reference_test,
                         testing::Values("empty_extent", "shorter", "equal", "vertical",
                                         "horizontal", "both", "negative_offset", "middle"),
                         [](const auto& info)
                         {
                             return std::string{info.param};
                         });

/// @brief A disabled axis sizes naturally while enabled axes use the owner's own finite policy.
TEST_F(scroll_fixture, ConstraintsAndDisabledAxisDoNotCreateCircularMeasure)
{
    tree.set_panel(viewport, scroll_layout{true, false, {1000, 1000}});
    resize_content({300, 120});
    auto style = tree.layout(viewport);
    style.constraints.maximum = {70, 60};
    style.constraints.minimum = {20, 30};
    tree.set_layout(viewport, style);
    const auto result = calculate();
    EXPECT_EQ(require_geometry(result.find(viewport)).desired, (layout_extent{20, 60}));
    EXPECT_EQ(scrolling(result).viewport, (layout_rect{0, 0, 70, 60}));
    EXPECT_EQ(scrolling(result).range, (layout_point{230, 0}));
    EXPECT_EQ(scrolling(result).offset, (layout_point{230, 0}));
    EXPECT_EQ(require_geometry(result.find(content)).bounds.height, 60);
}

/// @brief Inserting and removing natural stack children recalculates the finite content extent.
TEST_F(scroll_fixture, ContentInsertionRemovalAndFixedSizeChangesReclamp)
{
    tree.set_panel(content, vertical_layout{5});
    resize_content({0, 0});
    const auto first = rectangle(content);
    const auto second = rectangle(content);
    auto style = tree.layout(first);
    style.intrinsic = {50, 60};
    tree.set_layout(first, style);
    tree.set_layout(second, style);
    offset({0, std::numeric_limits<float>::max()});
    EXPECT_EQ(scrolling(calculate()).offset.y, 45);
    tree.remove(second);
    EXPECT_EQ(scrolling(calculate()).offset.y, 0);
    style.intrinsic.height = 150;
    tree.set_layout(first, style);
    EXPECT_EQ(scrolling(calculate()).offset.y, 70);
}
} // namespace atlas::ui::tests
