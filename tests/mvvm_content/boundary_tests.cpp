/** @file @brief Content limits, exact scalar preservation and strict schema vocabulary. */
#include <array>
#include <atlas/mvvm_content/document.hpp>
#include <bit>
#include <gtest/gtest.h>
#include <limits>

namespace atlas::mvvm_content::tests
{
/// @brief XML/codegen preserve unusual but valid scalar values without locale dependence.
TEST(mvvm_content, PreservesExtremeNumbersUnicodeAndEscapedText)
{
    mvvm::definition source{"model",
                            {{"minimum", std::numeric_limits<std::int64_t>::min()},
                             {"maximum", std::numeric_limits<std::int64_t>::max()},
                             {"small", std::numeric_limits<double>::denorm_min()},
                             {"negative_zero", -0.0},
                             {"text", std::string{"a\"\\\n\t & <> \xc3\xa9"}}}};
    const auto restored = parse_xml(write_xml(source));
    ASSERT_EQ(restored.fields.size(), source.fields.size());
    for (std::size_t i = 0; i < source.fields.size(); ++i)
    {
        EXPECT_EQ(restored.fields[i].initial, source.fields[i].initial);
    }

    EXPECT_EQ(std::bit_cast<std::uint64_t>(std::get<double>(restored.fields[3].initial)),
              std::bit_cast<std::uint64_t>(-0.0));
    EXPECT_NE(
        generate_factory_cpp(source, "make_edge").find("std::numeric_limits<std::int64_t>::min()"),
        std::string::npos);
}

/// @brief Parsing and output cannot evade byte/schema bounds through expansion or serialization.
TEST(mvvm_content, EnforcesInputOutputAndCollectionLimits)
{
    EXPECT_THROW(parse_xml("<ViewModel version='1' name='model'/>", {.max_bytes = 8}),
                 std::overflow_error);
    EXPECT_THROW(write_xml({"model"}, {.max_bytes = 8}), std::overflow_error);
    EXPECT_THROW(generate_factory_cpp({"x"}, "make_x", {.max_bytes = 1}), std::overflow_error);
    EXPECT_THROW(
        parse_xml(
            "<ViewModel version='1' name='m'><Fields><Field name='a' type='boolean' "
            "value='true'/><Field name='b' type='boolean' value='false'/></Fields></ViewModel>",
            {.schema = {.max_fields = 1}}),
        std::overflow_error);
}

/// @brief Unknown nodes and duplicate attributes never disappear silently from authored files.
class rejected_structure : public ::testing::TestWithParam<const char*>
{
};

/// @brief Reject content outside the versioned XML schema.
TEST_P(rejected_structure, RejectsUnexpectedContent)
{
    EXPECT_THROW(parse_xml(GetParam()), std::invalid_argument);
}

/// @brief Stable readable case names keep XML and malformed bytes out of test identifiers.
std::string structure_name(const ::testing::TestParamInfo<const char*>& info)
{
    static constexpr std::array names{
        "DuplicateAttribute", "UnexpectedText", "UnknownElement", "ProcessingInstruction",
        "MultipleRoots",      "InvalidBoolean", "UnknownType"};
    return names.at(info.index);
}

INSTANTIATE_TEST_SUITE_P(
    structure, rejected_structure,
    ::testing::Values(
        "<ViewModel version='1' name='x' name='y'/>",
        "<ViewModel version='1' name='x'><Fields>unexpected text</Fields></ViewModel>",
        "<ViewModel version='1' name='x'><Unknown/></ViewModel>",
        "<?external data?><ViewModel version='1' name='x'/>",
        "<ViewModel version='1' name='x'/><ViewModel version='1' name='y'/>",
        "<ViewModel version='1' name='x'><Fields><Field name='a' type='boolean' "
        "value='1'/></Fields></ViewModel>",
        "<ViewModel version='1' name='x'><Actions><Action name='go'><Param name='a' "
        "type='mystery'/></Action></Actions></ViewModel>"),
    structure_name);
} // namespace atlas::mvvm_content::tests
