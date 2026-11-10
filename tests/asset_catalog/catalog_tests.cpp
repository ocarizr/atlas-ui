/** @file @brief Asset identity allocation, relocation and immutable catalog compilation. */
#include <atlas/assets/catalog.hpp>
#include <gtest/gtest.h>

namespace atlas::assets::tests
{
/// @brief Reopening a moved catalog retains references and does not reuse removed ordinals.
TEST(persistent_assets, PreservesIdentityThroughMoveAndRestart)
{
    catalog_document draft{parse_namespace("018d0b95cf2673c6a0d7dd8ce5d29b23")};
    const asset_ref original{allocate_id(draft), asset_type::texture};
    const auto removed = allocate_id(draft);
    draft.records.push_back({original, {"textures", "old/icon.png"}, 4, 19});
    const auto initial = catalog::compile(draft);
    draft.records.front().source.path = "new/icon.png";
    const auto moved = read_xml({write_xml(catalog::compile(draft)), "reopened.xml"});
    EXPECT_EQ(initial.document().records.front().source.path, "old/icon.png");
    EXPECT_EQ(moved.resolve(original).revision, 4U);
    EXPECT_EQ(moved.resolve(original).byte_size, 19U);
    EXPECT_EQ(parse_id(format_id(original.id)), original.id);
    auto reopened = moved.document();
    EXPECT_GT(allocate_id(reopened).local, removed.local);
    EXPECT_THROW((void)moved.resolve({original.id, asset_type::script}), std::invalid_argument);
    EXPECT_THROW((void)moved.resolve({removed, asset_type::texture}), std::out_of_range);
}

/// @brief Copied metadata and foreign IDs cannot create ambiguous local references.
TEST(persistent_assets, RejectsDuplicatesAndForeignOwnership)
{
    catalog_document draft{parse_namespace("018d0b95cf2673c6a0d7dd8ce5d29b23")};
    draft.records.push_back({{allocate_id(draft), asset_type::ui}, {"ui", "menu.xml"}});
    auto copy = draft.records.front();
    copy.source.path = "copy.xml";
    draft.records.push_back(copy);
    EXPECT_THROW((void)catalog::compile(draft), std::invalid_argument);
    draft.records.back().reference.id = allocate_id(draft);
    draft.records.back().source.path = "MENU.XML";
    EXPECT_THROW((void)catalog::compile(draft), std::invalid_argument);
    draft.records.pop_back();
    draft.records.front().reference.id.space = parse_namespace("018d0b95cf2673c6a0d7dd8ce5d29b24");
    EXPECT_THROW((void)catalog::compile(draft), std::invalid_argument);
}

/// @brief Explicit dependency edges are typed and acyclic; derived links can point back to a
/// source.
TEST(persistent_assets, ValidatesDependencyClosureAndProductRoles)
{
    catalog_document draft{parse_namespace("018d0b95cf2673c6a0d7dd8ce5d29b23")};
    const asset_ref source{allocate_id(draft), asset_type::ui};
    const asset_ref preview{allocate_id(draft), asset_type::texture};
    draft.records = {{source, {"ui", "menu.xml"}}, {preview, {"textures", "preview.png"}}};
    draft.records[0].products = {{"preview", preview}};
    draft.records[1].dependencies = {source};
    EXPECT_NO_THROW((void)catalog::compile(draft));
    draft.records[0].dependencies = {preview};
    EXPECT_THROW((void)catalog::compile(draft), std::invalid_argument);
    draft.records[0].dependencies.clear();
    draft.records[0].products.front().target.type = asset_type::shader;
    EXPECT_THROW((void)catalog::compile(draft), std::invalid_argument);
    draft.records[0].products.front().target = preview;
    draft.records[0].products.push_back(draft.records[0].products.front());
    EXPECT_THROW((void)catalog::compile(draft), std::invalid_argument);
    draft.records[0].products.pop_back();
    draft.records.pop_back();
    EXPECT_THROW((void)catalog::compile(draft), std::invalid_argument);
}

/// @brief A malformed draft cannot lower the watermark and issue an already used identity.
TEST(persistent_assets, RejectsAllocationBelowExistingRecord)
{
    catalog_document draft{parse_namespace("018d0b95cf2673c6a0d7dd8ce5d29b23")};
    draft.records.push_back({{allocate_id(draft), asset_type::ui}, {"ui", "menu.xml"}});
    draft.next_local = 1;
    EXPECT_THROW((void)allocate_id(draft), std::invalid_argument);
    EXPECT_EQ(draft.next_local, 1U);
    EXPECT_THROW((void)catalog::compile(draft), std::invalid_argument);
}

/// @brief Reject oversized metadata before sorting individual records.
TEST(persistent_assets, RejectsUnboundedMetadata)
{
    catalog_document draft{parse_namespace("018d0b95cf2673c6a0d7dd8ce5d29b23")};
    draft.records.resize(4097);
    EXPECT_THROW((void)catalog::compile(std::move(draft)), std::invalid_argument);
}

/// @brief Invalid portable locators are rejected before any filesystem operation.
class invalid_asset_path : public testing::TestWithParam<const char*>
{
};

/// @brief Both directory traversal and Windows path aliases fail lexical validation.
TEST_P(invalid_asset_path, RejectsUnsafeLocator)
{
    catalog_document draft{parse_namespace("018d0b95cf2673c6a0d7dd8ce5d29b23")};
    draft.records.push_back({{allocate_id(draft), asset_type::script}, {"scripts", GetParam()}});
    EXPECT_THROW((void)catalog::compile(draft), std::invalid_argument);
}

INSTANTIATE_TEST_SUITE_P(paths, invalid_asset_path,
                         testing::Values("../script.lua", "C:/script.lua", "a\\b.lua", "a//b.lua",
                                         "a/./b.lua", "a/NUL.lua", "a/file.lua.", "file.lua:stream",
                                         "/script.lua", ""));
} // namespace atlas::assets::tests
