/** @file @brief Typed XML drag policy survives prepared construction and rejects ambiguity. */
#include "composition_fixture.hpp"
#include <atlas/ui/layout_engine.hpp>
#include <atlas/ui_content/prepared.hpp>

namespace atlas::ui_content::tests
{
/// @brief Drag policy round trips and builds prepared sources.
TEST_F(composition_fixture, DragPolicyRoundTripsAndBuildsPreparedSources)
{
    const auto source = parse_xml(
        {R"(<ui version="1"><element id="root" panel="canvas"/><element id="source" parent="root" width="20" height="20"><drag-source type="test.item" text="item-42" operations="copy-move" threshold="6"/></element><element id="target" parent="root"><drop-target types="test.item;atlas.asset" operations="copy"/></element></ui>)"});
    const auto bytes = encode_prepared({ui_asset, source, {{ui_asset, 1}}, {}, {}});
    EXPECT_EQ(std::to_integer<unsigned>(bytes.at(4)), 10U);
    const auto decoded = decode_prepared(bytes);
    EXPECT_EQ(write_xml(decoded.view), write_xml(source));
    auto view = build_prepared(decoded, {*storage});
    auto& tree = view->tree();
    tree.set_viewport({0, 0, 100, 80});
    ui::layout_engine layout{jobs.dispatcher()};
    const auto snapshot = tree.snapshot();
    auto& input = view->input();
    input.begin_frame(2, snapshot, layout.calculate(snapshot));
    input.process({});
    ui::drag_drop_session drag;
    const std::array views{&input};
    drag.begin_frame(1, views);
    ASSERT_TRUE(drag.lift(input, view->find("source")));
    drag.cancel();
    drag.end_frame();
}

/// @brief Rejects malformed policies before building.
TEST(drag_authoring_test, RejectsMalformedPoliciesBeforeBuilding)
{
    for (const auto* metadata :
         {R"(<drag-source type="bad type" text="x"/>)",
          R"(<drag-source type="test" threshold="-1"/>)", R"(<drop-target types="test;test"/>)",
          R"(<drop-target types=""/>)"})
    {
        const std::string xml =
            std::string{"<ui version=\"1\"><element id=\"root\">"} + metadata + "</element></ui>";
        EXPECT_THROW((void)parse_xml({xml}), std::invalid_argument) << metadata;
    }
}
} // namespace atlas::ui_content::tests
