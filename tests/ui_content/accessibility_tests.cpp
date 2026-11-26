/** @file @brief Accessible authoring metadata survives prepared construction exactly. */
#include "composition_fixture.hpp"
#include <atlas/ui/layout_engine.hpp>
#include <atlas/ui_content/prepared.hpp>

namespace atlas::ui_content::tests
{
/// @brief Verify accessibility metadata round trips and builds prepared instances.
TEST_F(composition_fixture, AccessibilityMetadataRoundTripsAndBuildsPreparedInstances)
{
    const auto source = parse_xml(
        {R"(<ui version="1"><element id="root" width="100" height="80"><accessibility role="group" name="Settings"/></element><element id="button" parent="root" width="20" height="20"><interaction focusable="true"/><event-action event="click" action="atlas.navigate_to" screen="home" route="open" input-action="42"/><accessibility role="button" name-key="settings.apply" invoke-action="42"/></element></ui>)"});
    auto bytes = encode_prepared({ui_asset, source, {{ui_asset, 1}}, {}, {}});
    EXPECT_EQ(std::to_integer<unsigned>(bytes.at(4)), 9U);
    auto decoded = decode_prepared(bytes);
    EXPECT_EQ(write_xml(decoded.view), write_xml(source));
    auto view = build_prepared(decoded, {*storage});
    auto& tree = view->tree();
    tree.set_viewport({0, 0, 100, 80});
    ui::layout_engine layout{jobs.dispatcher()};
    const auto snapshot = tree.snapshot();
    view->input().begin_frame(2, snapshot, layout.calculate(snapshot));
    auto nodes =
        view->input().accessibility_nodes({},
                                          [](std::string_view key)
                                          {
                                              return key == "settings.apply" ? "Apply" : "";
                                          });
    ASSERT_EQ(nodes.size(), 2);
    EXPECT_EQ(nodes[1].name, "Apply");
    EXPECT_TRUE(nodes[1].invoke);
}

/// @brief Verify rejects ambiguous localized text and unknown roles.
TEST(accessibility_authoring_test, RejectsAmbiguousLocalizedTextAndUnknownRoles)
{
    EXPECT_THROW(
        (void)parse_xml(
            {R"(<ui version="1"><element id="root"><accessibility role="invalid"/></element></ui>)"}),
        std::invalid_argument);
    EXPECT_THROW(
        (void)parse_xml(
            {R"(<ui version="1"><element id="root"><accessibility name="Apply" name-key="apply"/></element></ui>)"}),
        std::invalid_argument);
}

/// @brief An authored click works through accessibility without requiring a physical input mapping.
TEST_F(composition_fixture, AccessibilityInvokesTheExistingAuthoredClick)
{
    const auto source = parse_xml(
        {R"(<ui version="1"><element id="root" width="100" height="80" text="Resume"><interaction focusable="true"/><event-action event="click" action="atlas.navigate_to" screen="home" route="open"/></element></ui>)"});
    unsigned activated = 0;
    construction options{*storage};
    options.dispatch_event = [&](const event_action& action)
    {
        EXPECT_EQ(action.route, "open");
        ++activated;
    };
    auto view = instance::build(source, options);
    auto& tree = view->tree();
    tree.set_viewport({0, 0, 100, 80});
    ui::layout_engine layout{jobs.dispatcher()};
    const auto snapshot = tree.snapshot();
    auto& input = view->input();
    input.begin_frame(2, snapshot, layout.calculate(snapshot));
    const auto nodes = input.accessibility_nodes();
    ASSERT_EQ(nodes.size(), 1);
    EXPECT_TRUE(nodes.front().invoke);
    const accessibility::request request{nodes.front().id, accessibility::action::invoke};
    input.process_accessibility(std::span{&request, 1});
    EXPECT_EQ(activated, 1);
    EXPECT_EQ(input.focused(), ui::element_handle{});
}
} // namespace atlas::ui_content::tests
