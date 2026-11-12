/** @file @brief Native authoring workflow through Atlas input, preview and streamer persistence. */
#include "../../../streaming/temporary_assets.hpp"
#include "document_file.hpp"
#include "editor_view.hpp"
#include <gtest/gtest.h>

namespace atlas::editor::tests
{
namespace
{
/// @brief Send physical pointer samples through the same Atlas routes as the editor window.
void click(editor_view& view, const window::window_state& state, input::input_value position)
{
    const std::array events{input::event{.device = {1, input::device_kind::pointer},
                                         .control = input::pointer_control::primary,
                                         .value = {1, 0},
                                         .position = position},
                            input::event{.device = {1, input::device_kind::pointer},
                                         .control = input::pointer_control::primary,
                                         .position = position}};
    (void)view.snapshot(state, events);
}
} // namespace

/// @brief Click real controls, persist an authored edit and replace the preview by reopening it.
TEST(editor_authoring, EditsAndReopensThroughAtlasControls)
{
    streaming::tests::temporary_assets files;
    jobs::job_system jobs{2};
    const auto catalog =
        assets::catalog::compile({assets::parse_namespace("018d0b95cf2673c6a0d7dd8ce5d29b23")});
    streaming::catalog_streamer storage{jobs, catalog, {}};
    document_session session{new_document(), {storage}};
    editor_view view{session, jobs, files.path(), {}};
    const window::window_state state{960, 540, 1};
    const auto initial = view.snapshot(state, {});
    EXPECT_FALSE(initial->atlas->pages().empty());
    click(view, state, {750, 225});
    ASSERT_EQ(session.document().elements.size(), 3U);
    EXPECT_EQ(session.selected(), "element_1");
    click(view, state, {750, 190});
    ASSERT_FALSE(session.document().elements.back().requested.visible);
    click(view, state, {96, 18});
    ASSERT_FALSE(session.dirty());
    const auto saved = ui_content::write_xml(session.document());
    EXPECT_EQ(ui_content::write_xml(open_document(files.path(), jobs, {})), saved);
    click(view, state, {245, 18});
    EXPECT_TRUE(session.document().elements.back().requested.visible);
    click(view, state, {162, 18});
    EXPECT_EQ(ui_content::write_xml(session.document()), saved);
    EXPECT_FALSE(session.dirty());
    EXPECT_FALSE(session.can_undo());
    EXPECT_EQ(session.preview().size(), 3U);
}

/// @brief A failed serialization leaves the previously published document available.
TEST(editor_authoring, FailedSavePreservesPreviousXml)
{
    streaming::tests::temporary_assets files;
    jobs::job_system jobs{1};
    const auto source = new_document();
    save_document(files.path(), source, {});
    auto invalid = source;
    invalid.elements.back().parent = "missing";
    EXPECT_THROW(save_document(files.path(), invalid, {}), std::invalid_argument);
    EXPECT_EQ(ui_content::write_xml(open_document(files.path(), jobs, {})),
              ui_content::write_xml(source));
}

/// @brief A native replacement failure removes its temporary file.
TEST(editor_authoring, FailedReplacementRetiresTemporaryFile)
{
    streaming::tests::temporary_assets files;
    const auto destination = files.path();
    ASSERT_TRUE(std::filesystem::create_directory(destination));
    EXPECT_THROW(save_document(destination, new_document(), {}), std::system_error);
    for (const auto& item : std::filesystem::directory_iterator(destination.parent_path()))
    {
        EXPECT_EQ(item.path(), destination);
    }
}
} // namespace atlas::editor::tests
