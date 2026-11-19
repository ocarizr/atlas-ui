/** @file @brief Typed presentation XML, catalog identity and prepared equivalence. */
#include <atlas/ui_content/flow.hpp>
#include <atlas/ui_content/prepared.hpp>
#include <gtest/gtest.h>

namespace atlas::ui_content::tests
{
/// @brief Execute the same source contract for each explicitly authored UI kind.
class presentation_kind_test : public testing::TestWithParam<assets::asset_type>
{
};

/// @brief Each kind retains exact persisted identity through typed XML round trips.
TEST_P(presentation_kind_test, RetainsTypedIdentity)
{
    const auto type = GetParam();
    EXPECT_EQ(assets::parse_type(assets::format_type(type)), type);
    EXPECT_TRUE(assets::is_ui(type));
    const auto xml = "<ui version='2' kind='" + std::string{assets::format_type(type)} +
                     "'><element id='root'/></ui>";
    const auto source = parse_xml({xml});
    EXPECT_EQ(source.presentation.type, type);
    EXPECT_EQ(parse_xml({write_xml(source)}).presentation, source.presentation);
}

INSTANTIATE_TEST_SUITE_P(kinds, presentation_kind_test,
                         testing::Values(assets::asset_type::menu, assets::asset_type::submenu,
                                         assets::asset_type::popup, assets::asset_type::widget),
                         [](const auto& info)
                         {
                             return std::string{assets::format_type(info.param)};
                         });

/// @brief Popup policies belong to popup documents and never silently change other kinds.
TEST(presentation_contract, RoundTripsPopupPoliciesAndRejectsInvalidRoots)
{
    const auto source = parse_xml({R"(<ui version="2" kind="popup" modal="false"
        dismiss-outside="true" dismiss-back="false"><element id="root"/></ui>)"});
    EXPECT_FALSE(source.presentation.popup.modal);
    EXPECT_TRUE(source.presentation.popup.dismiss_outside);
    EXPECT_FALSE(source.presentation.popup.dismiss_back);
    EXPECT_EQ(parse_xml({write_xml(source)}).presentation, source.presentation);
    EXPECT_THROW(
        (void)parse_xml({"<ui version='2' kind='menu' modal='false'><element id='root'/></ui>"}),
        std::invalid_argument);
    EXPECT_THROW((void)parse_xml({"<ui version='2' kind='video'><element id='root'/></ui>"}),
                 std::invalid_argument);
}

/// @brief Prepared construction retains the authored role and every popup policy bit.
TEST(presentation_contract, PreparedPayloadRetainsPresentationAndCatalogKind)
{
    const assets::asset_ref ref{{assets::parse_namespace("018d0b95cf2673c6a0d7dd8ce5d29b23"), 1},
                                assets::asset_type::popup};
    const auto source = parse_xml({R"(<ui version="2" kind="popup" modal="false"
        dismiss-outside="true"><element id="root"/></ui>)"});
    const prepared_view prepared{ref, source, {{ref, 1}}, {}, {}};
    EXPECT_EQ(std::to_integer<unsigned>(encode_prepared(prepared).at(4)), 3U);
    EXPECT_EQ(decode_prepared(encode_prepared(prepared)).view.presentation, source.presentation);
    auto incorrect = prepared;
    incorrect.source.type = assets::asset_type::menu;
    EXPECT_THROW((void)encode_prepared(incorrect), std::invalid_argument);
}

/// @brief Existing source documents retain the original prepared UI v2 representation.
TEST(presentation_contract, KeepsLegacyPreparedVersion)
{
    const assets::asset_ref ref{{assets::parse_namespace("018d0b95cf2673c6a0d7dd8ce5d29b23"), 1},
                                assets::asset_type::ui};
    const auto source = parse_xml({"<ui version='1'><element id='root'/></ui>"});
    EXPECT_EQ(source.presentation.type, assets::asset_type::ui);
    EXPECT_NE(write_xml(source).find("version=\"1\""), std::string::npos);
    EXPECT_FALSE(assets::is_ui(assets::asset_type::video));
    const prepared_view prepared{ref, source, {{ref, 1}}, {}, {}};
    const auto bytes = encode_prepared(prepared);
    EXPECT_EQ(std::to_integer<unsigned>(bytes.at(4)), 2U);
    EXPECT_EQ(encode_prepared(decode_prepared(bytes)), bytes);
}
} // namespace atlas::ui_content::tests
