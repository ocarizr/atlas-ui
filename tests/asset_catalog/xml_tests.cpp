/** @file @brief Strict catalog XML version, canonical round trips and source diagnostics. */
#include <atlas/assets/catalog.hpp>
#include <gtest/gtest.h>

namespace atlas::assets::tests
{
/// @brief Canonical XML retains typed metadata, byte extents and import versions.
TEST(asset_catalog_xml, CanonicalizesAssetOrderAndRetainsMetadata)
{
    catalog_document draft{parse_namespace("018d0b95cf2673c6a0d7dd8ce5d29b23")};
    const asset_ref menu{allocate_id(draft), asset_type::ui};
    const asset_ref script{allocate_id(draft), asset_type::script};
    draft.records = {{script, {"scripts", "menu.lua"}, 3, 14, "lua", 2, 4},
                     {menu, {"ui", "menu.xml"}, 1, 0, "xml", 5, 6, {script}}};
    const auto first = catalog::compile(draft);
    const auto xml = write_xml(first);
    const auto second = read_xml({xml, "catalog.xml"});
    EXPECT_EQ(second.document(), first.document());
    EXPECT_EQ(second.document().records.front().reference, menu);
    EXPECT_EQ(write_xml(second), xml);
}

/// @brief Numeric XML escapes cannot truncate a validated logical locator at an embedded NUL.
TEST(asset_catalog_xml, RejectsEscapedLocatorAliases)
{
    catalog_document draft{parse_namespace("018d0b95cf2673c6a0d7dd8ce5d29b23")};
    draft.records.push_back({{allocate_id(draft), asset_type::script}, {"scripts", "menu.lua"}});
    auto xml = write_xml(catalog::compile(draft));
    const auto path = xml.find("menu.lua");
    ASSERT_NE(path, std::string::npos);
    xml.insert(path + 8, "&#0;hidden");
    EXPECT_THROW((void)read_xml({xml, "catalog.xml"}), std::invalid_argument);
}

/// @brief Malformed metadata reports the caller-provided name instead of a generic load failure.
class malformed_catalog : public testing::TestWithParam<const char*>
{
};

/// @brief Schema errors cannot produce a partial catalog snapshot.
TEST_P(malformed_catalog, RejectsUnknownOrMalformedDocument)
{
    try
    {
        (void)read_xml({GetParam(), "project/assets.xml"});
        FAIL() << "expected catalog parse failure";
    }
    catch (const std::invalid_argument& error)
    {
        EXPECT_NE(std::string_view{error.what()}.find("project/assets.xml"),
                  std::string_view::npos);
    }
}

INSTANTIATE_TEST_SUITE_P(
    documents, malformed_catalog,
    testing::Values(
        "<asset-catalog", "<manifest/>",
        "<asset-catalog version='2' namespace='018d0b95cf2673c6a0d7dd8ce5d29b23' next-local='1'/>",
        "<asset-catalog version='1' version='1' namespace='018d0b95cf2673c6a0d7dd8ce5d29b23' "
        "next-local='1'/>",
        "<asset-catalog version='1' namespace='018d0b95cf2673c6a0d7dd8ce5d29b23' "
        "next-local='1'><unknown/></asset-catalog>",
        "<!DOCTYPE asset-catalog><asset-catalog version='1' "
        "namespace='018d0b95cf2673c6a0d7dd8ce5d29b23' next-local='1'/>"));
} // namespace atlas::assets::tests
