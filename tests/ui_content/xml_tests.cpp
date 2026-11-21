/** @file @brief Strict UI document source and deterministic round trips. */
#include <atlas/ui_content/document.hpp>
#include <atlas/ui_content/prepared.hpp>
#include <gtest/gtest.h>

namespace atlas::ui_content::tests
{
/// @brief A scroll viewport remains a typed container after an XML round trip.
TEST(ui_document_xml, RoundTripsScrollViewport)
{
    const auto source = parse_xml({R"(<ui version="1">
      <element id="viewport" panel="scroll" scroll-horizontal="true" scroll-y="75" width="100" height="50"/>
      <element id="content" parent="viewport" panel="vertical"/>
    </ui>)"});
    const auto xml = write_xml(source);
    EXPECT_NE(xml.find("panel=\"scroll\""), std::string::npos);
    EXPECT_EQ(write_xml(parse_xml({xml})), xml);
    const auto ref =
        assets::asset_ref{assets::parse_id("018d0b95cf2673c6a0d7dd8ce5d29b23:0000000000000001"),
                          assets::asset_type::ui};
    const prepared_view prepared{ref, source, {{ref, 1}}, {}, {}};
    const auto bytes = encode_prepared(prepared);
    EXPECT_EQ(std::to_integer<unsigned>(bytes.at(4)), 5U);
    EXPECT_EQ(write_xml(decode_prepared(bytes).view), xml);
}

/// @brief Canonical XML preserves durable references and the order of children.
TEST(ui_document_xml, RoundTripsPersistentReferences)
{
    const auto source = parse_xml({R"(<ui version="1">
      <model name="hud" asset="018d0b95cf2673c6a0d7dd8ce5d29b23:0000000000000001"/>
      <element id="root" panel="canvas"/>
      <element id="icon" parent="root" panel="leaf" width="23">
        <paint fill="true" texture="018d0b95cf2673c6a0d7dd8ce5d29b23:0000000000000002"/>
        <bind model="hud" field="active" property="visible"/>
        <action model="hud" name="label">
          <arg type="text" value="Save &amp; &quot;close&quot;&#10;Next"/>
        </action>
      </element>
    </ui>)"});
    const auto xml = write_xml(source);
    const auto reopened = parse_xml({xml});
    ASSERT_EQ(reopened.elements.size(), 2U);
    EXPECT_EQ(reopened.elements[1].parent, "root");
    EXPECT_EQ(reopened.elements[1].texture, source.elements[1].texture);
    EXPECT_EQ(reopened.elements[1].requested.width, 23.f);
    EXPECT_EQ(std::get<std::string>(reopened.elements[1].actions[0].binding.arguments[0]),
              "Save & \"close\"\nNext");
    EXPECT_EQ(write_xml(reopened), xml);
}

/// @brief Unknown attributes, duplicate IDs and orphaned elements name their source.
TEST(ui_document_xml, RejectsMalformedAndUnresolvedSource)
{
    for (const auto* xml :
         {"<ui version='1'><element id='root' bad='1'/></ui>",
          "<ui version='1'><element id='root'/><element id='root' parent='root'/></ui>",
          "<ui version='1'><element id='root'/><element id='orphan' parent='bad'/></ui>",
          "<ui version='1'><element id='root&#0;suffix'/></ui>",
          "<ui version='1'><element id='root' opacity='nan'/></ui>",
          "<ui version='1'><element id='root'></ui>"})
    {
        try
        {
            (void)parse_xml({xml, "dashboard.ui"});
            ADD_FAILURE() << "Accepted " << xml;
        }
        catch (const std::invalid_argument& error)
        {
            EXPECT_NE(std::string_view(error.what()).find("dashboard.ui"), std::string_view::npos);
        }
    }
}
} // namespace atlas::ui_content::tests
