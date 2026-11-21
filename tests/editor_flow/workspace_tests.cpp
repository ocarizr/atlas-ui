/** @file @brief Graph gestures and edits preserve identities and one undoable transaction. */
#include "fixture.hpp"
#include <atlas/editor_flow/workspace.hpp>

namespace atlas::editor_flow::tests
{
/// @brief Settings tabs connect within their owning region; cross-region routes are rejected.
TEST(flow_workspace, CreatesTypedSettingsRegionsAndConnections)
{
    session owner{example()};
    workspace tool{owner};
    const auto menu = example().graph.screens.front().asset;
    auto submenu = menu;
    submenu.type = assets::asset_type::submenu;
    submenu.id.local = 2;
    tool.add_region({"tabs", "audio", "home", "content", true});
    tool.add_screen({"audio", "tabs", submenu}, {40, 60});
    tool.add_screen({"video", "tabs", submenu}, {240, 60});
    tool.connect({"video_tab", "audio", "video", ui_content::flow_operation::replace});
    EXPECT_EQ(owner.current().graph.regions.back().host, "content");
    EXPECT_EQ(tool.routes("audio").front().id, "video_tab");
    EXPECT_TRUE(tool.routes("home").empty());
    EXPECT_THROW(tool.connect({"wrong_region", "home", "audio"}), std::invalid_argument);
    EXPECT_EQ(owner.current().graph.transitions.size(), 1U);
    EXPECT_TRUE(owner.undo());
    EXPECT_TRUE(owner.current().graph.transitions.empty());
}

/// @brief Motion is transient until release and cancellation never enters undo history.
TEST(flow_workspace, DragIsOneRevisionAndCancelHasNoSideEffects)
{
    session owner{example()};
    workspace tool{owner};
    tool.select({item_kind::screen, "home"});
    const auto revision = owner.revision();
    tool.begin_move();
    tool.move_by({50, 70});
    tool.move_by({100, 80});
    EXPECT_EQ(owner.revision(), revision);
    EXPECT_DOUBLE_EQ(tool.position({item_kind::screen, "home"}).x, 110);
    tool.end_move(true);
    EXPECT_DOUBLE_EQ(owner.current().layout.front().x, 10);
    tool.begin_move();
    tool.move_by({80, 40});
    tool.end_move();
    EXPECT_DOUBLE_EQ(owner.current().layout.front().x, 90);
    EXPECT_TRUE(owner.undo());
    EXPECT_DOUBLE_EQ(owner.current().layout.front().x, 10);
}

/// @brief Multi-node edits preserve stable routes through undo and remove owned links on deletion.
TEST(flow_workspace, MultiselectionMoveAndDeletionKeepReferencesSound)
{
    session owner{example()};
    workspace tool{owner};
    tool.add_screen({"other", "viewport", example().graph.screens.front().asset}, {200, 20});
    tool.connect({"open", "home", "other"});
    tool.select({item_kind::screen, "home"});
    tool.select({item_kind::screen, "other"}, true);
    tool.begin_move();
    tool.move_by({5, 7});
    tool.end_move();
    EXPECT_DOUBLE_EQ(tool.position({item_kind::screen, "other"}).x, 205);
    tool.erase_selection();
    EXPECT_TRUE(owner.current().graph.screens.empty());
    EXPECT_TRUE(owner.current().graph.transitions.empty());
    EXPECT_TRUE(owner.current().graph.regions.front().initial.empty());
    EXPECT_TRUE(owner.undo());
    EXPECT_EQ(owner.current().graph.screens.size(), 2U);
    EXPECT_EQ(owner.current().graph.transitions.front().id, "open");
}

/// @brief Zoom is view-only and stale drag completion cannot overwrite an independent edit.
TEST(flow_workspace, ZoomPreservesPointAndRejectsStaleGesture)
{
    session owner{example()};
    workspace tool{owner};
    const point anchor{120, 60};
    const auto before = tool.to_graph(anchor);
    tool.zoom_at(2, anchor);
    EXPECT_EQ(tool.to_graph(anchor), before);
    EXPECT_EQ(tool.to_view(before), anchor);
    EXPECT_FALSE(owner.dirty());
    tool.select({item_kind::screen, "home"});
    tool.begin_move();
    tool.move_by({1, 1});
    auto edit = owner.current();
    edit.layout.front().label = "Changed";
    owner.apply(edit);
    EXPECT_THROW(tool.end_move(), std::logic_error);
    EXPECT_EQ(owner.current().layout.front().label, "Changed");
    EXPECT_THROW(tool.zoom_at(0, anchor), std::invalid_argument);
}
} // namespace atlas::editor_flow::tests
