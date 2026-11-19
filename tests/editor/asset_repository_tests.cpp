/** @file @brief Catalog transactions, stable identity and delayed import publication. */
#include "asset_repository.hpp"
#include <gtest/gtest.h>
#include <map>

namespace atlas::editor::tests
{
/// @brief Controllable preparation independent of repository lifetime.
struct asset_delivery
{
    bool ready = false;     ///< Complete the accepted acquisition.
    bool cancelled = false; ///< Observe cancellation.
    asset_payload payload;  ///< Owned output.
};

/// @brief A controllable acquisition has no worker borrowing editor state.
class controlled_asset_read final : public asset_read
{
  public:
    explicit controlled_asset_read(std::shared_ptr<asset_delivery> output)
        : output_{std::move(output)}
    {
    } ///< Retain fixture state independently.

    bool ready() const override
    {
        return output_->ready;
    } ///< Owner-controlled readiness.

    asset_payload take() override
    {
        return output_->payload;
    } ///< Shared byte ownership.

    void cancel() noexcept override
    {
        output_->cancelled = true;
    } ///< Cancellation observation.
  private:
    std::shared_ptr<asset_delivery> output_; ///< Fixture-owned state.
};

/// @brief Isolated atomic storage, with one controllable accepted read.
class memory_asset_store final : public asset_store
{
  public:
    std::unique_ptr<asset_read> read(const std::filesystem::path&, assets::asset_type) override
    {
        return std::make_unique<controlled_asset_read>(delivery);
    } ///< Polling is controlled by the test.

    std::filesystem::path locate(const assets::source_locator& source) const override
    {
        return source.root + "/" + source.path;
    } ///< Exact logical locator for these model tests.

    bool exists(const assets::source_locator& source) const override
    {
        return files.contains(locate(source).generic_string());
    } ///< Metadata-only observation.

    void create(const assets::source_locator& target, std::span<const std::uint8_t> bytes) override
    {
        if (!files.emplace(locate(target).generic_string(), std::string(bytes.begin(), bytes.end()))
                 .second)
        {
            throw std::invalid_argument("occupied source");
        }
    } ///< Refuse overwrite.

    void rollback(const assets::source_locator& target) override
    {
        files.erase(locate(target).generic_string());
    } ///< Delete only a candidate owned by this transaction.

    void publish(const assets::catalog& snapshot) override
    {
        if (fail)
        {
            throw std::runtime_error("fixture persistence failure");
        }

        xml = assets::write_xml(snapshot);
    } ///< Failure preserves previous metadata.

    bool fail = false;                        ///< Inject a replacement failure.
    std::string xml;                          ///< Most recent committed catalog.
    std::map<std::string, std::string> files; ///< Private source payloads.
    /// @brief Retained output for the accepted read.
    std::shared_ptr<asset_delivery> delivery = std::make_shared<asset_delivery>();
};

/// @brief Reuse one deterministic identity namespace and tiny payload.
class asset_repository_test : public testing::Test
{
  protected:
    /// @brief Empty owned origin.
    assets::catalog initial =
        assets::catalog::compile({assets::parse_namespace("018d0b95cf2673c6a0d7dd8ce5d29b23")});
    std::shared_ptr<memory_asset_store> store =
        std::make_shared<memory_asset_store>();  ///< Backend.
    asset_repository repository{initial, store}; ///< Destroy acquisition before the store.

    /// @brief Complete a small import through the public asynchronous boundary.
    assets::asset_ref import_asset(std::string name)
    {
        repository.begin({"external", {"texture", std::move(name)}, assets::asset_type::texture});
        store->delivery->payload.bytes =
            std::make_shared<const streaming::asset_bytes>(4, std::uint8_t{255});
        store->delivery->payload.digest = std::string(64, 'a');
        store->delivery->ready = true;
        EXPECT_TRUE(repository.poll());
        return repository.snapshot().document().records.back().reference;
    }
};

/// @brief Relocation persists the same typed identity and undo never reuses exposed IDs.
TEST_F(asset_repository_test, ImportMoveReopenAndUndoPreserveIdentity)
{
    const auto ref = import_asset("first.bin");
    repository.begin({"texture/first.bin", {"texture", "moved.bin"}, ref.type, ref});
    ASSERT_TRUE(repository.poll());
    EXPECT_EQ(repository.snapshot().document().records.front().reference, ref);
    EXPECT_EQ(repository.snapshot().document().records.front().source.path, "moved.bin");
    asset_repository reopened{assets::read_xml({store->xml}), store};
    EXPECT_EQ(reopened.snapshot().resolve(ref).revision, 2U);
    ASSERT_TRUE(repository.undo());
    EXPECT_EQ(repository.snapshot().document().records.front().source.path, "first.bin");
    ASSERT_TRUE(repository.undo());
    const auto replacement = import_asset("replacement.bin");
    EXPECT_GT(replacement.id.local, ref.id.local);
    EXPECT_FALSE(repository.redo());
}

/// @brief Failed metadata replacement rolls back the private source and preserves published state.
TEST_F(asset_repository_test, PersistenceFailureLeavesNoPublishedCandidate)
{
    const auto ref = import_asset("first.bin");
    const auto before = assets::write_xml(repository.snapshot());
    repository.begin({"external", {"texture", "new.bin"}, ref.type});
    store->fail = true;
    EXPECT_THROW(repository.poll(), std::runtime_error);
    EXPECT_EQ(assets::write_xml(repository.snapshot()), before);
    EXPECT_FALSE(store->files.contains("texture/new.bin"));
}

/// @brief Cancellation and a more recent owner mutation reject late successful preparation.
TEST_F(asset_repository_test, CancelAndChangedGenerationRejectLateResults)
{
    const auto ref = import_asset("first.bin");
    store->delivery->ready = false;
    repository.begin({"external", {"texture", "late.bin"}, ref.type});
    repository.cancel();
    EXPECT_TRUE(store->delivery->cancelled);
    store->delivery->ready = true;
    EXPECT_FALSE(repository.poll());
    EXPECT_FALSE(store->files.contains("texture/late.bin"));
    repository.begin({"external", {"texture", "late.bin"}, ref.type});
    repository.configure(ref, 2);
    EXPECT_FALSE(repository.poll());
    EXPECT_EQ(repository.snapshot().document().records.size(), 1U);
}

/// @brief Required edges block removal and source edits invalidate dependent revisions.
TEST_F(asset_repository_test, ReferencesAndImporterChangesInvalidateDependents)
{
    const auto texture = import_asset("icon.bin");
    store->delivery->payload.dependencies = {texture};
    const auto material = import_asset("material.bin");
    EXPECT_EQ(repository.dependents(texture), (std::vector{material}));
    EXPECT_THROW(repository.remove(texture), std::invalid_argument);
    const auto old = repository.snapshot().resolve(material).revision;
    repository.configure(texture, 3);
    EXPECT_GT(repository.snapshot().resolve(material).revision, old);
    repository.remove(material);
    EXPECT_NO_THROW(repository.remove(texture));
}

/// @brief Search and missing rows share metadata, with strict path rejection before acquisition.
TEST_F(asset_repository_test, BrowseAndRejectedPathsPreserveThePublishedCatalog)
{
    const auto ref = import_asset("Folder/Icon.bin");
    EXPECT_EQ(repository.browse(ref.type, "icon").size(), 1U);
    EXPECT_TRUE(repository.browse(assets::asset_type::audio).empty());
    store->files.clear();
    EXPECT_TRUE(repository.browse(ref.type).front().missing);
    EXPECT_THROW(repository.begin({"external", {"texture", "../escape"}, ref.type}),
                 std::invalid_argument);
    EXPECT_THROW(repository.begin({"external", {"audio", "wrong"}, ref.type}),
                 std::invalid_argument);
    EXPECT_EQ(repository.snapshot().document().records.size(), 1U);
}

/// @brief Unchanged refreshes preserve revisions and do not add redundant undo steps.
TEST_F(asset_repository_test, UnchangedRefreshDoesNotPublish)
{
    const auto ref = import_asset("icon.bin");
    const auto before = store->xml;
    repository.begin({{}, {"texture", "icon.bin"}, ref.type, ref, false});
    EXPECT_FALSE(repository.poll());
    EXPECT_EQ(store->xml, before);
    EXPECT_EQ(repository.snapshot().resolve(ref).revision, 1U);
}

/// @brief An incomplete worker fingerprint cannot enter persistent metadata.
TEST_F(asset_repository_test, RejectsMissingFingerprintBeforeSourceCreation)
{
    const auto ref = import_asset("icon.bin");
    repository.begin({"external", {"texture", "bad.bin"}, ref.type});
    store->delivery->payload.digest.clear();
    EXPECT_THROW(repository.poll(), std::invalid_argument);
    EXPECT_FALSE(store->files.contains("texture/bad.bin"));
    EXPECT_EQ(repository.snapshot().document().records.size(), 1U);
}
} // namespace atlas::editor::tests
