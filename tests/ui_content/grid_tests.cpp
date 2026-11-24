/** @file @brief Grid XML and prepared contracts construct the same portable geometry. */
#include "composition_fixture.hpp"
#include <atlas/ui/layout_engine.hpp>
#include <atlas/ui_content/prepared.hpp>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace atlas::ui_content::tests
{
namespace
{
/// @brief Serialized data stays in the context fixture directory, outside repeated test bodies.
document grid_document()
{
    const auto path = std::filesystem::path{ATLAS_UI_DOCUMENT}.parent_path() / "grid.xml";
    std::ifstream file{path};
    if (!file)
    {
        throw std::runtime_error("missing grid XML fixture");
    }

    return parse_xml(
        {std::string{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}},
         "grid.xml"});
}
} // namespace

/// @brief Grid metadata uses a new version without changing legacy panel tags or source semantics.
TEST_F(composition_fixture, grid_xml_and_prepared_have_equivalent_geometry)
{
    const auto source = grid_document();
    const auto bytes = encode_prepared({ui_asset, source, {{ui_asset, 1}}, {}, {}});
    EXPECT_EQ(std::to_integer<unsigned>(bytes.at(4)), 7);
    const auto prepared = decode_prepared(bytes);
    EXPECT_EQ(write_xml(prepared.view), write_xml(source));
    auto view = build_prepared(prepared, {*storage});
    ui::layout_engine layout{jobs.dispatcher()};
    const auto geometry = layout.update(view->tree(), {0, 0, 100, 80});
    EXPECT_EQ(geometry.find(view->find("spanning")).value_or(ui::layout_geometry{}).bounds,
              (ui::layout_rect{0, 0, 54, 28}));
}

/// @brief Invalid relationships and incomplete coordinate pairs fail before runtime publication.
TEST(grid_authoring, invalid_parent_and_slot_coordinates_are_rejected)
{
    EXPECT_THROW((void)parse_xml({"<ui version='1'><element id='root'/><element id='child' "
                                  "parent='root'><grid-slot row='0' column='0'/></element></ui>"}),
                 std::invalid_argument);
    EXPECT_THROW((void)parse_xml({"<ui version='1'><element id='root' panel='grid'/><element "
                                  "id='child' parent='root'><grid-slot row='0'/></element></ui>"}),
                 std::invalid_argument);
    auto source = grid_document();
    source.elements.push_back(source.elements.back());
    source.elements.back().id = "overlap";
    EXPECT_THROW((void)write_xml(source), std::invalid_argument);
}

/// @brief Rewriting only the envelope version cannot smuggle new panel tags into a legacy decoder.
TEST(grid_authoring, prepared_grid_requires_its_declared_wire_version)
{
    const assets::asset_ref asset{
        assets::parse_id("018d0b95cf2673c6a0d7dd8ce5d29b23:0000000000000001"),
        assets::asset_type::ui};
    auto bytes = encode_prepared({asset, grid_document(), {{asset, 1}}, {}, {}});
    bytes[4] = std::byte{6};
    EXPECT_THROW((void)decode_prepared(bytes), std::invalid_argument);
}
} // namespace atlas::ui_content::tests
