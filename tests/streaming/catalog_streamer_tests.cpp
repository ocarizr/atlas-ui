/** @file @brief Equivalent typed loose/range reads and coordinator publication checks. */
#include "temporary_assets.hpp"
#include <atlas/streaming/catalog_streamer.hpp>
#include <atomic>
#include <gtest/gtest.h>

namespace atlas::streaming::tests
{
namespace
{
/// @brief Build the same typed asset metadata for every backing layout.
assets::catalog example_catalog()
{
    assets::catalog_document draft{assets::parse_namespace("018d0b95cf2673c6a0d7dd8ce5d29b23")};
    draft.records.push_back(
        {{assets::allocate_id(draft), assets::asset_type::ui}, {"ui", "menu.bin"}, 3, 4});
    return assets::catalog::compile(std::move(draft));
}

/// @brief Register a range within a shared backing product containing unrelated bytes.
storage_mount packed(const assets::catalog& catalog)
{
    auto bytes = std::make_shared<const asset_bytes>(asset_bytes{'X', 'A', 'B', 'C', 'D', 'Y'});
    return {{{"legacy_pack", memory_asset{std::move(bytes)}}},
            {{catalog.document().records.front().reference, 3, "legacy_pack", 1}}};
}
} // namespace

/// @brief An authored reference and asset-relative offset survive a storage-layout change.
TEST(typed_catalog_streaming, ResolvesEquivalentLooseAndPackRanges)
{
    temporary_assets files;
    files.write();
    jobs::job_system pool{2};
    const auto catalog = example_catalog();
    const auto reference = catalog.document().records.front().reference;
    const std::array roots{source_root{"ui", files.path().parent_path()}};
    catalog_streamer loose{pool, catalog, prepare_loose_mount(catalog, roots)};
    catalog_streamer package{pool, catalog, packed(catalog)};
    for (auto* mount : {&loose, &package})
    {
        EXPECT_EQ(mount->describe(reference).byte_size, 4U);
        mount
            ->read({reference, 1, 2, 3},
                   [mount](read_stamp stamp, const read_result& result)
                   {
                       ASSERT_TRUE(mount->accepts(stamp));
                       ASSERT_TRUE(
                           std::holds_alternative<std::shared_ptr<const asset_bytes>>(result));
                       EXPECT_EQ(*std::get<std::shared_ptr<const asset_bytes>>(result),
                                 (asset_bytes{'B', 'C'}));
                   })
            .get();
    }
}

/// @brief A ready result from the previous mount cannot publish after a same-revision replacement.
TEST(typed_catalog_streaming, RejectsStaleCompletionAtPublication)
{
    jobs::job_system pool{2};
    const auto catalog = example_catalog();
    const auto reference = catalog.document().records.front().reference;
    read_stamp previous_stamp;
    catalog_streamer previous{pool, catalog, packed(catalog)};
    auto ready = previous.read(
        {reference, 0, 4},
        [&](read_stamp stamp, const read_result& result)
        {
            EXPECT_TRUE(std::holds_alternative<std::shared_ptr<const asset_bytes>>(result));
            previous_stamp = stamp;
        });
    catalog_streamer current{pool, catalog, packed(catalog)};
    ready.get();
    EXPECT_TRUE(previous.accepts(previous_stamp));
    EXPECT_FALSE(current.accepts(previous_stamp));
    EXPECT_EQ(previous_stamp.revision, 3U);
}

/// @brief Validate routes atomically and reject reads crossing logical asset boundaries.
TEST(typed_catalog_streaming, RejectsIncompleteOrStaleMountAndInvalidRead)
{
    jobs::job_system pool{1};
    const auto catalog = example_catalog();
    const auto reference = catalog.document().records.front().reference;
    auto broken = packed(catalog);
    broken.routes.front().revision = 2;
    EXPECT_THROW((catalog_streamer{pool, catalog, broken}), std::invalid_argument);
    broken.routes.front().revision = 3;
    broken.routes.front().offset = UINT64_MAX;
    EXPECT_THROW((catalog_streamer{pool, catalog, broken}), std::out_of_range);
    broken.routes.clear();
    EXPECT_THROW((catalog_streamer{pool, catalog, broken}), std::invalid_argument);

    std::atomic_bool called{};
    const auto callback = [&](read_stamp, const read_result&)
    {
        called = true;
    };
    catalog_streamer mount{pool, catalog, packed(catalog)};
    EXPECT_THROW(mount.read({reference, 0, 1, 2}, callback), std::invalid_argument);
    EXPECT_THROW(mount.read({reference, 3, 2}, callback), std::out_of_range);
    EXPECT_THROW(mount.read({{reference.id, assets::asset_type::texture}, 0, 1}, callback),
                 std::invalid_argument);
    mount.shutdown();
    EXPECT_FALSE(called.load());
}

/// @brief Loose preparation fails before publication if the root or source is unavailable.
TEST(typed_catalog_streaming, RejectsUnauthorizedAndMissingLooseSources)
{
    temporary_assets files;
    const auto catalog = example_catalog();
    const std::array wrong{source_root{"textures", files.path().parent_path()}};
    EXPECT_THROW((void)prepare_loose_mount(catalog, wrong), std::invalid_argument);
    const std::array roots{source_root{"ui", files.path().parent_path()}};
    EXPECT_THROW((void)prepare_loose_mount(catalog, roots), std::invalid_argument);
    files.write();
    EXPECT_NO_THROW((void)prepare_loose_mount(catalog, roots));
}

/// @brief Persist and reopen an actual relink without changing the serialized reference.
TEST(typed_catalog_streaming, ReopensRenamedSourceWithTheSameReference)
{
    temporary_assets files;
    files.write();
    const auto old_catalog = example_catalog();
    auto draft = old_catalog.document();
    const auto reference = draft.records.front().reference;
    const auto root = files.path().parent_path();
    const std::array roots{source_root{"ui", root}};
    std::filesystem::rename(files.path(), root / "renamed.bin");
    EXPECT_THROW((void)prepare_loose_mount(old_catalog, roots), std::invalid_argument);
    draft.records.front().source.path = "renamed.bin";
    const auto reopened =
        assets::read_xml({assets::write_xml(assets::catalog::compile(draft)), "reopened.xml"});
    jobs::job_system pool{1};
    catalog_streamer mount{pool, reopened, prepare_loose_mount(reopened, roots)};
    mount
        .read({reference, 0, 4, 3},
              [](read_stamp, const read_result& result)
              {
                  ASSERT_TRUE(std::holds_alternative<std::shared_ptr<const asset_bytes>>(result));
                  EXPECT_EQ(*std::get<std::shared_ptr<const asset_bytes>>(result),
                            (asset_bytes{'A', 'B', 'C', 'D'}));
              })
        .get();
}

/// @brief Canonical containment rejects a valid locator redirected outside its root.
TEST(typed_catalog_streaming, RejectsLinkOutsideAuthorizedRoot)
{
    temporary_assets inside;
    temporary_assets outside;
    outside.write();
    std::error_code error;
    std::filesystem::create_symlink(outside.path(), inside.path(), error);
    if (error)
    {
        GTEST_SKIP() << "File symlink creation is unavailable: " << error.message();
    }

    const auto catalog = example_catalog();
    const std::array roots{source_root{"ui", inside.path().parent_path()}};
    EXPECT_THROW((void)prepare_loose_mount(catalog, roots), std::invalid_argument);
}

/// @brief The typed layer preserves legacy pre-cancellation and still returns an exact stamp.
TEST(typed_catalog_streaming, ForwardsCancellation)
{
    jobs::job_system pool{1};
    const auto catalog = example_catalog();
    const auto reference = catalog.document().records.front().reference;
    catalog_streamer mount{pool, catalog, packed(catalog)};
    std::stop_source cancellation;
    cancellation.request_stop();
    read_options options;
    options.cancellation = cancellation.get_token();
    mount
        .read(
            {reference, 0, 1},
            [&](read_stamp stamp, const read_result& result)
            {
                EXPECT_TRUE(mount.accepts(stamp));
                ASSERT_TRUE(std::holds_alternative<std::exception_ptr>(result));
                EXPECT_THROW(std::rethrow_exception(std::get<std::exception_ptr>(result)),
                             jobs::job_cancelled);
            },
            options)
        .get();
}
} // namespace atlas::streaming::tests
