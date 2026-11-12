/** @file @brief Validate editor command history and independent live preview state. */
#include "document_session.hpp"
#include <gtest/gtest.h>

namespace atlas::editor::tests
{
/// @brief Isolate authoring and preview on an asset-free typed mount.
class document_test : public testing::Test
{
  protected:
    /// @brief Create a valid empty runtime catalog with a stable namespace.
    static assets::catalog catalog()
    {
        return assets::catalog::compile(
            {assets::parse_namespace("018d0b95cf2673c6a0d7dd8ce5d29b23")});
    }

    jobs::job_system jobs{1};                                 ///< Outlives streamer and preview.
    streaming::catalog_streamer storage{jobs, catalog(), {}}; ///< Asset-free source mount.
    document_session session{new_document(), {storage}};      ///< Isolated editable view.
};

/// @brief Edits publish matching runtime values and history restores authored stable selection.
TEST_F(document_test, UndoRedoAndBranchMaintainSavedState)
{
    session.mark_saved();
    ASSERT_TRUE(session.apply(edit_kind::add_child));
    const auto id = std::string{session.selected()};
    ASSERT_TRUE(session.apply(edit_kind::toggle_visible));
    EXPECT_FALSE(session.document().elements.back().requested.visible);
    EXPECT_TRUE(session.dirty());
    ASSERT_TRUE(session.undo());
    EXPECT_TRUE(session.document().elements.back().requested.visible);
    ASSERT_TRUE(session.undo());
    EXPECT_FALSE(session.dirty());
    ASSERT_TRUE(session.redo());
    EXPECT_EQ(session.selected(), id);
    ASSERT_TRUE(session.apply(edit_kind::wider));
    EXPECT_FALSE(session.can_redo());
}

/// @brief Preview-only changes never mutate the document or leak into a rebuilt preview.
TEST_F(document_test, PreviewIsIndependentAndFailedReplacePreservesState)
{
    ASSERT_TRUE(session.apply(edit_kind::add_child));
    const auto before = ui_content::write_xml(session.document());
    const auto root = session.preview().snapshot().elements().front().target;
    session.preview().remove(session.preview().first_child(root));
    EXPECT_EQ(ui_content::write_xml(session.document()), before);
    auto invalid = session.document();
    invalid.elements.back().parent = "missing";
    EXPECT_THROW(session.replace(std::move(invalid), true), std::invalid_argument);
    EXPECT_EQ(ui_content::write_xml(session.document()), before);
    ASSERT_TRUE(session.undo());
    ASSERT_TRUE(session.redo());
    EXPECT_EQ(session.preview().size(), session.document().elements.size());
}
} // namespace atlas::editor::tests
