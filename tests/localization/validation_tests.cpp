/** @file @brief Invalid table and XML inputs cannot become a published localization catalog. */
#include "support.hpp"
#include <limits>

namespace atlas::localization::tests
{
/// @brief Named table mutations share one validation fixture rather than duplicating setup.
class invalid_catalog : public ::testing::TestWithParam<unsigned>
{
};

/// @brief Validate programmatically prepared data through the same contract as XML input.
TEST_P(invalid_catalog, rejects_invalid_definition)
{
    auto data = fixture();
    switch (GetParam())
    {
        case 0:
            data.fallback = "de";
            break;
        case 1:
            data.languages.push_back(data.languages[0]);
            break;
        case 2:
            data.languages[0].messages.push_back(data.languages[0].messages[0]);
            break;
        case 3:
            data.languages[1].messages[0].key = "unknown";
            break;
        case 4:
            data.languages[0].messages.back().forms[5].reset();
            break;
        case 5:
            data.languages[0].messages[0].text = "\xff";
            break;
        case 6:
            data.languages[0].messages[0].forms[5] = "ambiguous";
            break;
        case 7:
            data.languages[0].messages[0].key.clear();
            break;
        case 8:
            data.languages[0].tag = "bad_underscore";
            break;
        case 9:
            data.languages[0].messages[0].text = std::string(16385, 'a');
            break;
        default:
            data.languages.clear();
            break;
    }

    EXPECT_THROW((void)context(std::move(data), fake_rule), std::invalid_argument);
}

INSTANTIATE_TEST_SUITE_P(invariants, invalid_catalog, ::testing::Range(0U, 11U));

/// @brief Strict schema parsing rejects unexpected structure, malformed data and unknown forms.
class invalid_xml : public ::testing::TestWithParam<const char*>
{
};

/// @brief Invalid XML never produces a usable table.
TEST_P(invalid_xml, rejects_malformed_authored_content)
{
    EXPECT_THROW((void)parse_xml(GetParam()), std::invalid_argument);
}

INSTANTIATE_TEST_SUITE_P(
    schema, invalid_xml,
    ::testing::Values("<wrong/>", "<localization fallback='en'><language tag='en'/></localization>",
                      "<!DOCTYPE x><localization/>", "<localization fallback='en' unknown='x'/>",
                      "<localization fallback='en'><language tag='en' direction='ltr'><plural "
                      "key='x'><form category='bad'>x</form></plural></language></localization>",
                      "<localization fallback='en'><language tag='en' direction='ltr'><string "
                      "key='x'><child/></string></language></localization>"));

/// @brief Counts and adapter results are validated even when no native backend is injected.
TEST_F(context_fixture, rejects_invalid_counts_and_adapter_results)
{
    EXPECT_THROW((void)source.resolve(source.key("items"), std::numeric_limits<double>::infinity()),
                 std::invalid_argument);
    context bad{fixture(), [](auto, auto)
                {
                    // Deliberately reject an unknown representable wire category.
                    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
                    return static_cast<plural_category>(255);
                }};
    EXPECT_THROW((void)bad.resolve(bad.key("items"), 1), std::invalid_argument);
}

/// @brief Duplicate XML attributes cannot silently select one authored language contract.
TEST(localization_xml, rejects_duplicate_attributes)
{
    EXPECT_THROW(
        (void)parse_xml(
            R"(<localization fallback="en" fallback="pt"><language tag="en" direction="ltr"/></localization>)"),
        std::invalid_argument);
}

/// @brief UTF-8 validation rejects overlong, surrogate, truncated and embedded-NUL strings.
TEST(localization_unicode, rejects_invalid_scalars)
{
    for (const std::string& bad :
         {std::string{"\xc0\x80"}, std::string{"\xed\xa0\x80"}, std::string{"\xf4\x90\x80\x80"},
          std::string{"\xc2"}, std::string{"a\0b", 3}})
    {
        auto data = fixture();
        data.languages[0].messages[0].text = bad;
        EXPECT_THROW((void)context(std::move(data), fake_rule), std::invalid_argument);
    }
}
} // namespace atlas::localization::tests
