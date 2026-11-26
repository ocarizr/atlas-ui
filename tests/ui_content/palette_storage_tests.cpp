/** @file @brief Typed palette reads, cancellation and rejected stale mount publication. */
#include <atlas/ui_content/palette.hpp>
#include <gtest/gtest.h>

namespace atlas::ui_content::tests
{
/// @brief Each test owns its job pool, immutable catalog and exact in-memory representation.
class palette_storage_test : public testing::Test
{
  protected:
    jobs::job_system jobs{1}; ///< Joined after the storage mount drains.
    assets::catalog_document catalog{
        assets::parse_namespace("018d0b95cf2673c6a0d7dd8ce5d29b23")}; ///< Isolated identity space.
    assets::asset_ref reference{assets::allocate_id(catalog),
                                assets::asset_type::palette}; ///< Typed persistent identity.
    std::unique_ptr<streaming::catalog_streamer> storage;     ///< Own one generation per test.

    void SetUp() override
    {
        const std::string xml =
            R"(<palette version="1" contract="menu"><color key="text" value="#FFFFFFFF"/></palette>)";
        catalog.records.push_back({reference, {"fixture", "palette.xml"}, 1, xml.size()});
        streaming::storage_mount mount;
        mount.sources.emplace(
            "palette", streaming::memory_asset{
                           std::make_shared<const streaming::asset_bytes>(xml.begin(), xml.end())});
        mount.routes.push_back({reference, 1, "palette", 0});
        storage = std::make_unique<streaming::catalog_streamer>(
            jobs, assets::catalog::compile(catalog), mount);
    } ///< No codec or palette consumer probes a file path.
};

/// @brief A candidate from another mount cannot overwrite the currently selected colors.
TEST_F(palette_storage_test, LoadsAndRejectsForeignGenerationBeforePublication)
{
    const auto candidate = load_palette(reference, {*storage});
    ui::palette_context context{candidate.value};
    auto stale = candidate;
    ++stale.stamp.mount_generation;
    EXPECT_THROW(publish_palette(context, stale, *storage), std::runtime_error);
    EXPECT_EQ(context.snapshot(), candidate.value);
    EXPECT_EQ(context.revision(), 1);
    publish_palette(context, candidate, *storage);
    EXPECT_EQ(context.revision(), 1);
}

/// @brief Pre-cancelled reads never parse or replace a current valid palette.
TEST_F(palette_storage_test, CancellationAndWrongTypeRejectBeforeRead)
{
    std::stop_source cancellation;
    cancellation.request_stop();
    construction options{*storage};
    options.reads.cancellation = cancellation.get_token();
    EXPECT_THROW((void)load_palette(reference, options), jobs::job_cancelled);
    auto wrong = reference;
    wrong.type = assets::asset_type::texture;
    EXPECT_THROW((void)load_palette(wrong, {*storage}), std::invalid_argument);
}
} // namespace atlas::ui_content::tests
