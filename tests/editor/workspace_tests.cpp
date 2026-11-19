/** @file @brief Independent document histories and transactional recovery checkpoints. */
#include "document_workspace.hpp"
#include <gtest/gtest.h>
#include <map>

namespace atlas::editor::tests
{
/// @brief A deterministic persistence seam with injected read and atomic replacement failures.
class workspace_test : public testing::Test
{
  protected:
    /// @brief Return independent XML values and preserve stored data when a write fails.
    document_storage persistence()
    {
        return {[this](const auto& path) -> std::optional<ui_content::document>
                {
                    const auto found = files.find(path);
                    return found == files.end()
                               ? std::nullopt
                               : std::optional{ui_content::parse_xml({found->second})};
                },
                [this](const auto& path, const auto& source)
                {
                    if (fail_write || path == fail_path)
                    {
                        throw std::runtime_error("fixture write failure");
                    }

                    files[path] = ui_content::write_xml(source);
                    ++writes;
                },
                [this](const auto& path)
                {
                    if (fail_remove)
                    {
                        throw std::runtime_error("fixture remove failure");
                    }

                    files.erase(path);
                }};
    }

    jobs::job_system jobs{1}; ///< Outlives composed documents.
    /// @brief Empty composition mount retained until all views have retired.
    streaming::catalog_streamer assets{
        jobs,
        assets::catalog::compile({assets::parse_namespace("018d0b95cf2673c6a0d7dd8ce5d29b23")}),
        {}};
    std::map<std::filesystem::path, std::string> files; ///< No shared state between tests.
    bool fail_write = false;                            ///< Fail before replacement.
    bool fail_remove = false;                           ///< Fail after an ordinary save.
    unsigned writes = 0;                                ///< Observe autosave deduplication.
    std::filesystem::path fail_path; ///< Reject one owner while other checkpoints succeed.
};

/// @brief Switching and closing preserve unrelated preview, selection and history.
TEST_F(workspace_test, DocumentsOwnIndependentHistoryAndPreview)
{
    document_workspace workspace{{assets}, persistence()};
    const auto first = workspace.open("first.xml");
    auto& original = workspace.active();
    ASSERT_TRUE(original.apply(edit_kind::add_child));
    const auto selected = std::string{original.selected()};
    const auto second = workspace.open("second.xml");
    EXPECT_FALSE(workspace.active().can_undo());
    EXPECT_EQ(workspace.open("first.xml"), first);
    EXPECT_EQ(workspace.active().selected(), selected);
    ASSERT_TRUE(workspace.active().undo());
    workspace.activate(second);
    EXPECT_EQ(workspace.active().document().elements.size(), 2U);
    workspace.close(second, close_policy::preserve);
    EXPECT_EQ(workspace.active_id(), first);
    EXPECT_TRUE(workspace.active().can_redo());
}

/// @brief Checkpoints remain dirty, use separate paths, and can restore after the owner exits.
TEST_F(workspace_test, CheckpointRestoresUnsavedDocumentWithoutReplacingSource)
{
    files["first.xml"] = ui_content::write_xml(new_document());
    const auto source = files.at("first.xml");
    {
        document_workspace workspace{{assets}, persistence()};
        (void)workspace.open("first.xml");
        ASSERT_TRUE(workspace.active().apply(edit_kind::add_child));
        workspace.checkpoint_all();
        EXPECT_TRUE(workspace.active().dirty());
        EXPECT_EQ(files.at("first.xml"), source);
        const auto after = writes;
        workspace.checkpoint_all();
        EXPECT_EQ(writes, after);
    }

    document_workspace reopened{{assets}, persistence()};
    (void)reopened.open("first.xml");
    EXPECT_EQ(reopened.active().document().elements.size(), 2U);
    EXPECT_TRUE(reopened.recover());
    EXPECT_EQ(reopened.active().document().elements.size(), 3U);
    EXPECT_TRUE(reopened.active().dirty());
    EXPECT_TRUE(reopened.active().can_undo());
    ASSERT_TRUE(reopened.active().undo());
    EXPECT_FALSE(reopened.active().dirty());
}

/// @brief Failed writes and malformed recovery never retire the active owner or original bytes.
TEST_F(workspace_test, FailureAndMalformedRecoveryPreserveLiveState)
{
    document_workspace workspace{{assets}, persistence()};
    const auto id = workspace.open("first.xml");
    auto& live = workspace.active();
    ASSERT_TRUE(live.apply(edit_kind::add_child));
    files[recovery_path("first.xml")] = "<ui";
    EXPECT_THROW((void)workspace.recover(), std::invalid_argument);
    EXPECT_EQ(&workspace.active(), &live);
    EXPECT_TRUE(live.can_undo());
    fail_write = true;
    EXPECT_THROW(workspace.save(), std::runtime_error);
    EXPECT_THROW(workspace.close(id, close_policy::preserve), std::runtime_error);
    EXPECT_EQ(workspace.active_id(), id);
    EXPECT_TRUE(live.dirty());
    fail_write = false;
    EXPECT_NO_THROW(workspace.close(id, close_policy::preserve));
    EXPECT_EQ(workspace.size(), 0U);
    EXPECT_THROW(workspace.activate(id), std::invalid_argument);
}

/// @brief Saving before recovery cleanup fails must retain dirty state for a safe retry.
TEST_F(workspace_test, SaveAndDiscardHaveExplicitRecoveryOwnership)
{
    document_workspace workspace{{assets}, persistence()};
    const auto id = workspace.open("first.xml");
    ASSERT_TRUE(workspace.active().apply(edit_kind::add_child));
    workspace.checkpoint_all();
    fail_remove = true;
    EXPECT_THROW(workspace.save(), std::runtime_error);
    EXPECT_TRUE(workspace.active().dirty());
    fail_remove = false;
    workspace.save();
    EXPECT_FALSE(workspace.active().dirty());
    EXPECT_FALSE(files.contains(recovery_path("first.xml")));
    ASSERT_TRUE(workspace.active().apply(edit_kind::wider));
    workspace.checkpoint_all();
    workspace.close(id, close_policy::discard);
    EXPECT_FALSE(files.contains(recovery_path("first.xml")));
}

/// @brief Invalid reopen, duplicate paths and close with a newer edit retain consistent owners.
TEST_F(workspace_test, ReopenAndShutdownPreserveTheLatestUnsavedRevision)
{
    files["first.xml"] = ui_content::write_xml(new_document());
    document_workspace workspace{{assets}, persistence()};
    const auto id = workspace.open("first.xml");
    ASSERT_TRUE(workspace.active().apply(edit_kind::add_child));
    workspace.checkpoint_all();
    ASSERT_TRUE(workspace.active().apply(edit_kind::wider));
    const auto latest = ui_content::write_xml(workspace.active().document());
    files["first.xml"] = "bad XML";
    EXPECT_THROW(workspace.reopen(), std::invalid_argument);
    EXPECT_EQ(ui_content::write_xml(workspace.active().document()), latest);
    workspace.close(id, close_policy::preserve);
    EXPECT_EQ(files.at(recovery_path("first.xml")), latest);
}

/// @brief A damaged source can be recovered explicitly without parsing or replacing its bytes.
TEST_F(workspace_test, ExplicitStartupRecoveryBypassesMalformedSource)
{
    files["first.xml"] = "damaged";
    auto checkpoint = new_document();
    checkpoint.elements.back().requested.width = 200;
    files[recovery_path("first.xml")] = ui_content::write_xml(checkpoint);
    document_workspace workspace{{assets}, persistence()};
    EXPECT_THROW((void)workspace.open("first.xml"), std::invalid_argument);
    EXPECT_EQ(workspace.size(), 0U);
    const auto id = workspace.open_recovery("first.xml");
    EXPECT_NE(id, 0U);
    EXPECT_TRUE(workspace.active().dirty());
    EXPECT_EQ(workspace.active().document().elements.back().requested.width, 200);
    EXPECT_EQ(files.at("first.xml"), "damaged");
}

/// @brief Capacity, reserved filenames and malformed opens never evict a live session.
TEST_F(workspace_test, RejectsInvalidAndExcessDocumentsWithoutEviction)
{
    document_workspace workspace{{assets}, persistence()};
    EXPECT_THROW((void)workspace.open(""), std::invalid_argument);
    EXPECT_THROW((void)workspace.create(recovery_path("first.xml")), std::invalid_argument);
    const auto first = workspace.open("first.xml");
    for (unsigned index = 1; index < 32; ++index)
    {
        (void)workspace.open("document-" + std::to_string(index) + ".xml");
    }

    EXPECT_THROW((void)workspace.open("excess.xml"), std::overflow_error);
    EXPECT_EQ(workspace.size(), 32U);
    EXPECT_NO_THROW(workspace.activate(first));
    files[recovery_path("reserved.xml")] = ui_content::write_xml(new_document());
    EXPECT_THROW((void)workspace.create("reserved.xml"), std::invalid_argument);
}

/// @brief Undoing back to saved content retires only the checkpoint written by this session.
TEST_F(workspace_test, SavedUndoRetiresThePreviouslyCheckpointedRevision)
{
    files["first.xml"] = ui_content::write_xml(new_document());
    document_workspace workspace{{assets}, persistence()};
    (void)workspace.open("first.xml");
    ASSERT_TRUE(workspace.active().apply(edit_kind::add_child));
    workspace.checkpoint_all();
    ASSERT_TRUE(files.contains(recovery_path("first.xml")));
    ASSERT_TRUE(workspace.active().undo());
    workspace.checkpoint_all();
    EXPECT_FALSE(files.contains(recovery_path("first.xml")));
    EXPECT_FALSE(workspace.active().dirty());
}

/// @brief Opening a missing source never replaces an earlier session's recovery with defaults.
TEST_F(workspace_test, ExistingRecoverySurvivesOpenAutosaveAndClose)
{
    auto recovered = new_document();
    recovered.elements.back().requested.width = 320;
    const auto previous = ui_content::write_xml(recovered);
    files[recovery_path("first.xml")] = previous;
    document_workspace workspace{{assets}, persistence()};
    const auto id = workspace.open("first.xml");
    EXPECT_TRUE(workspace.recovery_pending());
    workspace.checkpoint_all();
    EXPECT_EQ(files.at(recovery_path("first.xml")), previous);
    ASSERT_TRUE(workspace.active().apply(edit_kind::add_child));
    EXPECT_THROW(workspace.close(id, close_policy::preserve), std::logic_error);
    EXPECT_EQ(workspace.size(), 1U);
    EXPECT_EQ(files.at(recovery_path("first.xml")), previous);
    EXPECT_TRUE(workspace.recover());
    ASSERT_TRUE(workspace.active().undo());
    EXPECT_EQ(workspace.active().document().elements.size(), 3U);
    workspace.close(id, close_policy::preserve);
    EXPECT_EQ(ui_content::parse_xml({files.at(recovery_path("first.xml"))}).elements.size(), 3U);
}

/// @brief A failed document cannot prevent other owners from publishing their checkpoints.
TEST_F(workspace_test, PartialCheckpointFailureStillProtectsOtherDocuments)
{
    document_workspace workspace{{assets}, persistence()};
    (void)workspace.open("first.xml");
    (void)workspace.open("second.xml");
    fail_path = recovery_path("first.xml");
    EXPECT_THROW(workspace.checkpoint_all(), std::runtime_error);
    EXPECT_TRUE(files.contains(recovery_path("second.xml")));
    EXPECT_EQ(workspace.size(), 2U);
    fail_path.clear();
    EXPECT_NO_THROW(workspace.checkpoint_all());
    EXPECT_TRUE(files.contains(recovery_path("first.xml")));
}
} // namespace atlas::editor::tests
