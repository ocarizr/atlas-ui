/** @file @brief Real streamer reads and atomic graph-file replacement on Windows. */
#include "../../../streaming/temporary_assets.hpp"
#include "document_file.hpp"
#include "flow_file.hpp"
#include <gtest/gtest.h>

namespace atlas::editor::tests
{
/// @brief Temporary graph files are removed only after sessions and shared workers retire.
class flow_recovery_test : public testing::Test
{
  protected:
    streaming::tests::temporary_assets files; ///< Independent root per test.
    jobs::job_system jobs{2};                 ///< Application-equivalent shared worker lifetime.
};

/// @brief Incomplete graph authoring survives save, checkpoint, explicit recovery and reopen.
TEST_F(flow_recovery_test, saves_and_recovers_incomplete_graphs_through_native_storage)
{
    const auto path = files.path();
    const auto storage = make_flow_storage(jobs);
    editor_flow::document draft;
    draft.id = "graph";
    draft.graph.regions = {{"viewport", "missing", ""}};
    {
        editor_flow::persistence file{path, draft, storage};
        file.save();
        auto next = file.edit().current();
        next.graph.screens = {{"new_screen", "viewport", {}}};
        file.edit().apply(std::move(next));
        file.close(false);
    }
    ASSERT_TRUE(document_exists(editor_flow::recovery_path(path)));
    editor_flow::persistence reopened{path, storage};
    EXPECT_TRUE(reopened.recovery_pending());
    ASSERT_TRUE(reopened.recover());
    EXPECT_EQ(reopened.edit().current().graph.screens.front().id, "new_screen");
    const auto checkpoint = read_editor_source(editor_flow::recovery_path(path), jobs, {});
    std::filesystem::create_directory(path.parent_path() / "blocked");
    EXPECT_THROW(write_editor_source(path.parent_path() / "blocked", "not XML"), std::system_error);
    EXPECT_EQ(read_editor_source(editor_flow::recovery_path(path), jobs, {}), checkpoint);
    reopened.save();
    EXPECT_FALSE(document_exists(editor_flow::recovery_path(path)));
    EXPECT_EQ(editor_flow::read_xml({read_editor_source(path, jobs, {})}).graph.screens.front().id,
              "new_screen");
}
} // namespace atlas::editor::tests
