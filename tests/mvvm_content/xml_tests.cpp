/** @file @brief Authored XML round trips and strict rejection of ambiguous definitions. */
#include <array>
#include <atlas/mvvm_content/document.hpp>
#include <gtest/gtest.h>

namespace atlas::mvvm_content::tests
{
/// @brief Field creation is data-driven and the canonical writer preserves exact scalar values.
TEST(mvvm_xml, RoundTripsEditableDefinitionsWithoutNativeClasses)
{
    const auto source = parse_xml(R"(<ViewModel version="1" name="hud"><Fields>
        <Field name="xp" type="integer" value="-9223372036854775808"/>
        <Field name="title" type="text" value="a&amp;b&#10;c"/>
        </Fields><Actions><Action name="gain"><Param name="amount" type="integer"/></Action></Actions>
        <Events><Event name="done"/></Events></ViewModel>)");
    const auto canonical = write_xml(source);
    const auto restored = parse_xml(canonical);
    EXPECT_EQ(restored.name, "hud");
    ASSERT_EQ(restored.fields.size(), 2);
    EXPECT_EQ(restored.fields[0].initial, source.fields[0].initial);
    EXPECT_EQ(restored.fields[1].initial, source.fields[1].initial);
    EXPECT_EQ(write_xml(restored), canonical);
}

/// @brief Authoring errors fail before a schema/model is published.
class invalid_xml : public ::testing::TestWithParam<const char*>
{
};

/// @brief Unsupported or ambiguous authoring input is rejected explicitly.
TEST_P(invalid_xml, RejectsInvalidDocument)
{
    EXPECT_THROW(parse_xml(GetParam()), std::invalid_argument);
}

/// @brief Stable readable case names keep XML and malformed bytes out of test identifiers.
std::string invalid_xml_name(const ::testing::TestParamInfo<const char*>& info)
{
    static constexpr std::array names{"UnsupportedVersion", "UnknownAttribute", "NonfiniteNumber",
                                      "DuplicateField",     "Doctype",          "DuplicateGroup"};
    return names.at(info.index);
}

INSTANTIATE_TEST_SUITE_P(
    documents, invalid_xml,
    ::testing::Values("<ViewModel version='2' name='hud'/>",
                      "<ViewModel version='1' name='hud' extra='x'/>",
                      "<ViewModel version='1' name='hud'><Fields><Field name='x' type='number' "
                      "value='nan'/></Fields></ViewModel>",
                      "<ViewModel version='1' name='hud'><Fields><Field name='x' type='integer' "
                      "value='1'/><Field name='x' type='integer' value='2'/></Fields></ViewModel>",
                      "<!DOCTYPE ViewModel><ViewModel version='1' name='hud'/>",
                      "<ViewModel version='1' name='hud'><Fields/><Fields/></ViewModel>"),
    invalid_xml_name);
} // namespace atlas::mvvm_content::tests
