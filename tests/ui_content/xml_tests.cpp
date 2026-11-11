/** @file @brief Strict UI document source and deterministic round trips. */
#include <atlas/ui_content/document.hpp>
#include <gtest/gtest.h>

namespace atlas::ui_content::tests
{
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
