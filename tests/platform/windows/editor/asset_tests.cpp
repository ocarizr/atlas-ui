/** @file @brief Native asset acquisition, durable identity and authoring references. */
#include "../../../streaming/temporary_assets.hpp"
#include "asset_files.hpp"
#include "asset_panel.hpp"
#include "document_file.hpp"
#include "editor_view.hpp"
#include <Windows.h>
#include <gtest/gtest.h>
#include <thread>

namespace atlas::editor::tests
{
/// @brief Own isolated paths until all acquisitions and previews have drained.
class native_assets_test : public testing::Test
{
  protected:
    /// @brief Poll on the owner with a diagnostic deadline, without assuming worker timing.
    static bool finish(asset_repository& repository)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
        while (repository.busy() && std::chrono::steady_clock::now() < deadline)
        {
            if (repository.poll())
            {
                return true;
            }

            std::this_thread::yield();
        }

        if (repository.busy())
        {
            throw std::runtime_error("Timed out collecting editor asset import");
        }

        return false;
    }

    streaming::tests::temporary_assets files; ///< Retire after joined services.
    jobs::job_system workers{2};              ///< One shared application worker pool.
    std::filesystem::path root = files.path().parent_path() / "content"; ///< Private origin.
};

/// @brief Equal-length source changes are fingerprinted and reopening preserves typed identity.
TEST_F(native_assets_test, ImportsReopensRefreshesAndRetainsMoveRecovery)
{
    write_editor_source(files.path(), "abc");
    auto repository = open_asset_repository(root, workers, {});
    repository->begin({files.path(), {"texture", "icon.bin"}, assets::asset_type::texture});
    ASSERT_TRUE(finish(*repository));
    const auto record = repository->snapshot().document().records.front();
    EXPECT_EQ(record.source_hash,
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    auto reopened = open_asset_repository(root, workers, {});
    EXPECT_EQ(reopened->snapshot().document().records.front().reference, record.reference);
    write_editor_source(root / "texture/icon.bin", "xyz");
    reopened->begin({{}, record.source, record.reference.type, record.reference, false});
    ASSERT_TRUE(finish(*reopened));
    EXPECT_NE(reopened->snapshot().document().records.front().source_hash, record.source_hash);
    reopened->begin({root / "texture/icon.bin",
                     {"texture", "nested/moved.bin"},
                     record.reference.type,
                     record.reference});
    ASSERT_TRUE(finish(*reopened));
    EXPECT_TRUE(document_exists(root / "texture/icon.bin"));
    EXPECT_TRUE(document_exists(root / "texture/nested/moved.bin"));
    ASSERT_TRUE(reopened->undo());
    EXPECT_EQ(reopened->snapshot().document().records.front().source.path, "icon.bin");
    ASSERT_TRUE(reopened->redo());
    EXPECT_EQ(reopened->snapshot().document().records.front().source.path, "nested/moved.bin");
}

/// @brief A ready cancellation cannot publish, and an occupied destination is never overwritten.
TEST_F(native_assets_test, CancellationAndCollisionsPreserveSources)
{
    write_editor_source(files.path(), "abc");
    auto repository = open_asset_repository(root, workers, {});
    repository->begin({files.path(), {"texture", "cancelled.bin"}, assets::asset_type::texture});
    repository->cancel();
    EXPECT_FALSE(finish(*repository));
    EXPECT_FALSE(document_exists(root / "texture/cancelled.bin"));
    write_editor_source(root / "texture/occupied.bin", "original");
    EXPECT_THROW(
        repository->begin({files.path(), {"texture", "occupied.bin"}, assets::asset_type::texture}),
        std::invalid_argument);
    EXPECT_EQ(read_editor_source(root / "texture/occupied.bin", workers, {}), "original");
    EXPECT_TRUE(repository->snapshot().document().records.empty());
}

/// @brief Assignment uses stable references and protects live document undo history from removal.
TEST_F(native_assets_test, PanelAssignsReferencesAndProtectsDocumentHistory)
{
    write_editor_source(files.path(), "abc");
    auto repository = open_asset_repository(root, workers, {});
    repository->begin({files.path(), {"texture", "icon.bin"}, assets::asset_type::texture});
    ASSERT_TRUE(finish(*repository));
    const auto ref = repository->snapshot().document().records.front().reference;
    const auto mount = mount_editor_assets(repository->snapshot(), root, workers, {});
    document_workspace documents{{*mount}, make_document_storage(workers, {})};
    (void)documents.create(files.path().parent_path() / "menu.xml");
    asset_panel panel{*repository, root};
    ASSERT_TRUE(panel.execute({control_kind::assets_type, {}}, &documents, workers, {}));
    // Select by stable identity after cycling from UI through the other content categories.
    for (int i = 0; i < 3; ++i)
    {
        ASSERT_TRUE(panel.execute({control_kind::assets_type, {}}, &documents, workers, {}));
    }

    ASSERT_TRUE(panel.execute({control_kind::assets_select, assets::format_id(ref.id)}, &documents,
                              workers, {}));
    ASSERT_TRUE(panel.execute({control_kind::assets_assign, {}}, &documents, workers, {}));
    EXPECT_TRUE(documents.references_asset(ref));
    EXPECT_THROW(panel.execute({control_kind::assets_undo, {}}, &documents, workers, {}),
                 std::invalid_argument);
    documents.save();
    EXPECT_NE(read_editor_source(documents.path(), workers, {}).find(assets::format_id(ref.id)),
              std::string::npos);
    EXPECT_THROW(panel.execute({control_kind::assets_remove, {}}, &documents, workers, {}),
                 std::invalid_argument);
    ASSERT_TRUE(documents.active().undo());
    EXPECT_TRUE(documents.references_asset(ref));
    EXPECT_NO_THROW(documents.refresh_assets(mount));
}

/// @brief Failed durable publication cannot expose or retain a half-imported source.
TEST_F(native_assets_test, LockedCatalogRollsBackSourceAndReopensPreviousSnapshot)
{
    write_editor_source(files.path(), "abc");
    auto repository = open_asset_repository(root, workers, {});
    const auto before = assets::write_xml(repository->snapshot());
    const auto handle = CreateFileW((root / "catalog.xml").c_str(), GENERIC_READ, FILE_SHARE_READ,
                                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_NE(handle, INVALID_HANDLE_VALUE);
    const auto release = [](void* value)
    {
        (void)CloseHandle(value);
    };
    const std::unique_ptr<void, decltype(release)> lock{handle, release};
    repository->begin({files.path(), {"texture", "candidate.bin"}, assets::asset_type::texture});
    EXPECT_THROW(finish(*repository), std::system_error);
    EXPECT_FALSE(document_exists(root / "texture/candidate.bin"));
    EXPECT_EQ(assets::write_xml(repository->snapshot()), before);
    auto reopened = open_asset_repository(root, workers, {});
    EXPECT_EQ(assets::write_xml(reopened->snapshot()), before);
}

/// @brief The browser is reachable through normal Atlas pointer dispatch and retains old packets.
TEST_F(native_assets_test, AtlasToolbarOpensTheAssetBrowser)
{
    auto repository = open_asset_repository(root, workers, {});
    const auto mount = mount_editor_assets(repository->snapshot(), root, workers, {});
    document_workspace documents{{*mount}, make_document_storage(workers, {})};
    (void)documents.create(files.path().parent_path() / "menu.xml");
    editor_view view{documents, workers, {}};
    view.bind_assets(*repository, root);
    const auto before = view.snapshot({960, 540, 1}, {});
    ASSERT_TRUE(before);
    const auto retained_count = before->quads.size();
    const std::array events{input::event{.device = {1, input::device_kind::pointer},
                                         .control = input::pointer_control::primary,
                                         .value = {1, 0},
                                         .position = {920, 520}},
                            input::event{.device = {1, input::device_kind::pointer},
                                         .control = input::pointer_control::primary,
                                         .position = {920, 520}}};
    const auto browser = view.snapshot({960, 540, 1}, events);
    ASSERT_TRUE(browser);
    EXPECT_GT(browser->quads.size(), retained_count);
    EXPECT_EQ(before->quads.size(), retained_count);
    EXPECT_TRUE(browser->atlas);
}

/// @brief Losing an entire typed source folder remains recoverable through relink.
TEST_F(native_assets_test, MissingTypeFolderCanBeRelinkedWithoutChangingIdentity)
{
    write_editor_source(files.path(), "abc");
    auto repository = open_asset_repository(root, workers, {});
    repository->begin({files.path(), {"texture", "icon.bin"}, assets::asset_type::texture});
    ASSERT_TRUE(finish(*repository));
    const auto record = repository->snapshot().document().records.front();
    ASSERT_TRUE(std::filesystem::remove(root / "texture/icon.bin"));
    ASSERT_TRUE(std::filesystem::remove(root / "texture"));
    EXPECT_TRUE(repository->browse(record.reference.type).front().missing);
    repository->begin({files.path(), record.source, record.reference.type, record.reference});
    ASSERT_TRUE(finish(*repository));
    EXPECT_EQ(repository->snapshot().document().records.front().reference, record.reference);
    EXPECT_FALSE(repository->browse(record.reference.type).front().missing);
}

/// @brief Import interprets explicit UI types and rejects a catalog/source role disagreement.
TEST_F(native_assets_test, ImportsTypedMenuAndRejectsWrongPresentationKind)
{
    write_editor_source(files.path(), "<ui version='2' kind='menu'><element id='root'/></ui>");
    auto repository = open_asset_repository(root, workers, {});
    repository->begin({files.path(), {"menu", "settings.xml"}, assets::asset_type::menu});
    ASSERT_TRUE(finish(*repository));
    const auto record = repository->snapshot().document().records.front();
    EXPECT_EQ(record.reference.type, assets::asset_type::menu);
    auto reopened = open_asset_repository(root, workers, {});
    EXPECT_EQ(reopened->snapshot().document().records.front().reference, record.reference);
    repository->begin({files.path(), {"popup", "incorrect.xml"}, assets::asset_type::popup});
    EXPECT_THROW(finish(*repository), std::invalid_argument);
    EXPECT_FALSE(document_exists(root / "popup/incorrect.xml"));
    EXPECT_EQ(repository->snapshot().document().records.size(), 1U);
}
} // namespace atlas::editor::tests
