/** @file @brief Native editor checkpoint persistence and multi-document UI controls. */
#include "../../../streaming/temporary_assets.hpp"
#include "document_file.hpp"
#include "editor_view.hpp"
#include <gtest/gtest.h>

namespace atlas::editor::tests
{
/// @brief Give every test isolated paths and a worker pool that outlives the workspace.
class recovery_test : public testing::Test
{
  protected:
    /// @brief Route the real pointer release through the active Atlas chrome.
    static void click(editor_view& view, float x)
    {
        const std::array events{input::event{.device = {1, input::device_kind::pointer},
                                             .control = input::pointer_control::primary,
                                             .value = {1, 0},
                                             .position = {x, 520}},
                                input::event{.device = {1, input::device_kind::pointer},
                                             .control = input::pointer_control::primary,
                                             .position = {x, 520}}};
        (void)view.snapshot({960, 540, 1}, events);
    }

    streaming::tests::temporary_assets files; ///< Removed after readers and writers finish.
    jobs::job_system jobs{2};                 ///< Joined worker lifetime.
    /// @brief Empty composition mount retained until all views have retired.
    streaming::catalog_streamer assets{
        jobs,
        assets::catalog::compile({assets::parse_namespace("018d0b95cf2673c6a0d7dd8ce5d29b23")}),
        {}};
};

/// @brief Autosave and close preserve dirty bytes, while reopening requires explicit recovery.
TEST_F(recovery_test, CheckpointsAndRestoresThroughNativeStorage)
{
    const auto path = files.path();
    {
        document_workspace workspace{{assets}, make_document_storage(jobs, {})};
        (void)workspace.open(path);
        ASSERT_TRUE(workspace.active().apply(edit_kind::add_child));
        editor_view view{workspace, jobs, {}};
        ASSERT_TRUE(view.snapshot({960, 540, 1}, {}));
        view.autosave(std::chrono::steady_clock::now() + std::chrono::seconds{31});
        EXPECT_TRUE(document_exists(recovery_path(path)));
        EXPECT_FALSE(document_exists(path));
        EXPECT_TRUE(workspace.active().dirty());
        ASSERT_TRUE(workspace.active().apply(edit_kind::wider));
        EXPECT_TRUE(view.preserve());
    }

    document_workspace reopened{{assets}, make_document_storage(jobs, {})};
    (void)reopened.open_recovery(path);
    EXPECT_EQ(reopened.active().document().elements.size(), 3U);
    EXPECT_EQ(reopened.active().document().elements.back().requested.width, 136);
    reopened.save();
    EXPECT_FALSE(document_exists(recovery_path(path)));
    EXPECT_EQ(ui_content::write_xml(open_document(path, jobs, {})),
              ui_content::write_xml(reopened.active().document()));
}

/// @brief Native aliases select one owner; UI switching and closing retain independent state.
TEST_F(recovery_test, ToolbarSwitchesAndClosesTheActiveOwner)
{
    document_workspace workspace{{assets}, make_document_storage(jobs, {})};
    const auto first = workspace.open(files.path());
    auto second_path = files.path();
    second_path += ".second.xml";
    const auto second = workspace.open(second_path);
    EXPECT_EQ(workspace.open(second_path.parent_path() / "." / second_path.filename()), second);
    ASSERT_TRUE(workspace.active().apply(edit_kind::add_child));
    editor_view view{workspace, jobs, {}};
    const auto retained = view.snapshot({960, 540, 1}, {});
    ASSERT_TRUE(retained);
    const auto quad_count = retained->quads.size();
    click(view, 30);
    EXPECT_EQ(workspace.active_id(), first);
    EXPECT_FALSE(workspace.active().can_undo());
    click(view, 30);
    EXPECT_EQ(workspace.active_id(), second);
    click(view, 140);
    EXPECT_EQ(workspace.active_id(), first);
    EXPECT_TRUE(document_exists(recovery_path(second_path)));
    EXPECT_EQ(retained->quads.size(), quad_count);
    EXPECT_FALSE(retained->quads.empty());
}

/// @brief A failed atomic replacement keeps the old recovery file and active edits intact.
TEST_F(recovery_test, FailedCheckpointRetainsLiveDocumentAndPreviousCheckpoint)
{
    const auto path = files.path();
    document_workspace workspace{{assets}, make_document_storage(jobs, {})};
    (void)workspace.open(path);
    workspace.checkpoint_all();
    const auto checkpoint = recovery_path(path);
    const auto previous = ui_content::write_xml(open_document(checkpoint, jobs, {}));
    ASSERT_TRUE(workspace.active().apply(edit_kind::add_child));
    auto storage = make_document_storage(jobs, {});
    storage.write = [](const auto&, const auto&)
    {
        throw std::runtime_error("blocked storage");
    };
    document_workspace failing{{assets}, std::move(storage)};
    (void)failing.open_recovery(path);
    ASSERT_TRUE(failing.active().apply(edit_kind::add_child));
    editor_view view{failing, jobs, {}};
    EXPECT_FALSE(view.preserve());
    EXPECT_EQ(failing.size(), 1U);
    EXPECT_TRUE(failing.active().can_undo());
    EXPECT_EQ(ui_content::write_xml(open_document(checkpoint, jobs, {})), previous);
}
} // namespace atlas::editor::tests
