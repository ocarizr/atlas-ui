/** @file @brief Authored navigation survives XML/prepared content and constructs live bindings. */
#include "composition_fixture.hpp"
#include <atlas/ui/layout_engine.hpp>
#include <atlas/ui_content/prepared.hpp>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace atlas::ui_content::tests
{
/// @brief Keep authored relationships in the serialized fixture rather than repeated test strings.
document navigation_document()
{
    std::ifstream file{std::filesystem::path{ATLAS_UI_DOCUMENT}.parent_path() /
                       "focus_navigation.xml"};
    if (!file)
    {
        throw std::runtime_error("missing navigation XML fixture");
    }

    return parse_xml(
        {std::string{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}},
         "focus_navigation.xml"});
}

/// @brief A separate wire revision preserves older prepared documents and new relationship data.
TEST_F(composition_fixture, focus_navigation_xml_and_prepared_are_equivalent)
{
    const auto source = navigation_document();
    const auto bytes = encode_prepared({ui_asset, source, {{ui_asset, 1}}, {}, {}});
    EXPECT_EQ(std::to_integer<unsigned>(bytes.at(4)), 8U);
    EXPECT_EQ(write_xml(decode_prepared(bytes).view), write_xml(source));
}

/// @brief XML and prepared runtime construction resolve the same explicit targets and wrap policy.
TEST_F(composition_fixture, authored_focus_wiring_changes_only_focus)
{
    for (bool prepared : {false, true})
    {
        const auto source = navigation_document();
        auto view = prepared ? build_prepared(decode_prepared(encode_prepared(
                                                  {ui_asset, source, {{ui_asset, 1}}, {}, {}})),
                                              {*storage})
                             : instance::build(source, {*storage});
        auto& tree = view->tree();
        tree.set_viewport({0, 0, 100, 80});
        const auto snapshot = tree.snapshot();
        ui::layout_engine layout{jobs.dispatcher()};
        auto& input = view->input();
        input.begin_frame(2, snapshot, layout.calculate(snapshot));
        input.focus(view->find("first"));
        input.process({});
        EXPECT_TRUE(input.navigate(ui::focus_direction::right));
        EXPECT_EQ(input.focused(), view->find("third"));
        EXPECT_TRUE(input.navigate(ui::focus_direction::right));
        EXPECT_EQ(input.focused(), view->find("first"));
        input.tick();
        input.late_tick();
        input.end_frame();
        tree.apply(tree.commands().seal());
    }
}

/// @brief Invalid graph references and ambiguous action routes fail before prepared publication.
TEST(navigation_authoring, rejects_invalid_references_and_ambiguous_actions)
{
    for (unsigned scenario = 0; scenario < 5; ++scenario)
    {
        auto source = navigation_document();
        auto& focus = source.elements[1].focus;
        ASSERT_TRUE(focus.has_value());
        auto& node = *focus;
        switch (scenario)
        {
            case 0:
                node.destinations[1] = "missing";
                break;
            case 1:
                source.elements[3].interaction = ui::interaction_options{.focusable = false};
                break;
            case 2:
                node.blocked[1] = true;
                break;
            case 3:
                source.elements[0].focus_scope =
                    ui::focus_navigation_scope{.actions = {.next = 81}};
                break;
            default:
                source.elements[1].interaction.reset();
                break;
        }

        EXPECT_THROW((void)write_xml(source), std::invalid_argument);
    }
}

/// @brief Changing only the wire header cannot smuggle new descriptors into an older decoder.
TEST(navigation_authoring, rejects_downgraded_prepared_navigation)
{
    const assets::asset_ref asset{
        assets::parse_id("018d0b95cf2673c6a0d7dd8ce5d29b23:0000000000000001"),
        assets::asset_type::ui};
    auto bytes = encode_prepared({asset, navigation_document(), {{asset, 1}}, {}, {}});
    bytes[4] = std::byte{7};
    EXPECT_THROW((void)decode_prepared(bytes), std::invalid_argument);
}
} // namespace atlas::ui_content::tests
