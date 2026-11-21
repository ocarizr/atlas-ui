/** @file @brief Editor Flow gestures, text entry and recovery through the real Atlas panel path. */
#include "../../../streaming/temporary_assets.hpp"
#include "document_file.hpp"
#include "editor_view.hpp"
#include "flow_file.hpp"
#include "flow_panel.hpp"
#include "flow_project.hpp"
#include <gtest/gtest.h>

namespace atlas::editor::tests
{
/// @brief A new empty tool does not create unnecessary source or recovery files on shutdown.
TEST(flow_bootstrap, UntouchedEmptyGraphDoesNotWriteRecovery)
{
    streaming::tests::temporary_assets files;
    jobs::job_system jobs{1};
    auto graph = open_flow_document(files.path(), jobs, {});
    flow_panel panel{*graph, {}, nullptr, {}, "018d0b95cf2673c6a0d7dd8ce5d29b23"};
    panel.checkpoint();
    EXPECT_FALSE(std::filesystem::exists(files.path()));
    EXPECT_FALSE(std::filesystem::exists(editor_flow::recovery_path(files.path())));
}

/// @brief Explicitly restoring an empty graph remains recoverable until it is saved or discarded.
TEST(flow_bootstrap, ExplicitEmptyRecoveryIsNotMistakenForUntouchedBootstrap)
{
    streaming::tests::temporary_assets files;
    jobs::job_system jobs{1};
    editor_flow::document empty;
    empty.id = "empty_flow";
    empty.graph.regions = {{"viewport", "", ""}};
    const auto storage = make_flow_storage(jobs, {});
    storage.write(editor_flow::recovery_path(files.path()), editor_flow::write_xml(empty));
    auto graph = open_flow_document(files.path(), jobs, {}, true);
    flow_panel panel{*graph, {}, nullptr, {}, "018d0b95cf2673c6a0d7dd8ce5d29b23"};
    panel.checkpoint();
    EXPECT_TRUE(std::filesystem::exists(editor_flow::recovery_path(files.path())));
    EXPECT_FALSE(std::filesystem::exists(files.path()));
}

/// @brief Independent window/storage services and a minimal graph per native scenario.
class flow_workspace_test : public testing::Test
{
  protected:
    streaming::tests::temporary_assets files; ///< Isolated source/recovery directory.
    jobs::job_system jobs{1};                 ///< Joined after all UI and streamer users retire.
    assets::catalog catalog = assets::catalog::compile(
        {assets::parse_namespace("018d0b95cf2673c6a0d7dd8ce5d29b23")}); ///< Empty typed mount.
    streaming::catalog_streamer storage{jobs, catalog, {}}; ///< No direct consumer file I/O.
    document_session ui{new_document(), {storage}}; ///< Ordinary UI document remains independent.
    window::window_state state{960, 540, 1};        ///< Native logical editor extent.

    void SetUp() override
    {
        ui.mark_saved();
    } ///< Start with a clean independent UI baseline.

    void click(editor_view& view, float x, float y)
    {
        const std::array events{input::event{.device = {1, input::device_kind::pointer},
                                             .control = input::pointer_control::primary,
                                             .value = {1, 0},
                                             .position = {x, y}},
                                input::event{.device = {1, input::device_kind::pointer},
                                             .control = input::pointer_control::primary,
                                             .position = {x, y}}};
        (void)view.snapshot(state, events);
    } ///< Send ordered physical press/release through the editor's normal input bridge.
};

/// @brief The visible graph tool edits/saves/reopens its own XML while the UI remains unchanged.
TEST_F(flow_workspace_test, GraphDragSaveUndoAndReopenUseIndependentDocument)
{
    editor_flow::document initial;
    initial.id = "test_flow";
    initial.graph.regions = {{"viewport", "home", ""}};
    initial.graph.screens = {{"home", "viewport", {{}, assets::asset_type::menu}}};
    initial.layout = {{{editor_flow::item_kind::screen, "home"}, "Home", 10, 20}};
    editor_flow::persistence graph{files.path(), initial, make_flow_storage(jobs, {})};
    editor_view view{ui, jobs, files.path().parent_path() / "ui.xml", {}};
    view.bind_flow(graph, nullptr, {}, "018d0b95cf2673c6a0d7dd8ce5d29b23");
    ASSERT_TRUE(view.snapshot(state, {}));
    click(view, 820, 524);
    const std::array drag{input::event{.device = {1, input::device_kind::pointer},
                                       .control = input::pointer_control::primary,
                                       .value = {1, 0},
                                       .position = {40, 100}},
                          input::event{.kind = input::event_kind::pointer_motion,
                                       .device = {1, input::device_kind::pointer},
                                       .value = {80, 130}},
                          input::event{.device = {1, input::device_kind::pointer},
                                       .control = input::pointer_control::primary,
                                       .position = {80, 130}}};
    ASSERT_TRUE(view.snapshot(state, drag));
    EXPECT_DOUBLE_EQ(graph.edit().current().layout.front().x, 50);
    EXPECT_DOUBLE_EQ(graph.edit().current().layout.front().y, 50);
    EXPECT_FALSE(ui.dirty());
    click(view, 670, 18); // Save Flow, independent from the toolbar's UI save.
    EXPECT_FALSE(graph.edit().dirty());
    editor_flow::persistence reopened{files.path(), make_flow_storage(jobs, {})};
    EXPECT_DOUBLE_EQ(reopened.edit().current().layout.front().x, 50);
    EXPECT_TRUE(graph.edit().undo());
    EXPECT_DOUBLE_EQ(graph.edit().current().layout.front().x, 10);
    EXPECT_TRUE(view.preserve());
    EXPECT_TRUE(std::filesystem::exists(editor_flow::recovery_path(files.path())));
}

/// @brief Native control commands author a semantic route in UI XML without creating Lua wiring.
TEST_F(flow_workspace_test, EventInspectorWritesUiOwnerAndPreservesGraphIdentity)
{
    const auto selected = ui.document().elements.back().id;
    ui.select(selected);
    editor_flow::document initial;
    initial.id = "test_flow";
    initial.graph.regions = {{"viewport", "home", ""}};
    initial.graph.screens = {{"home", "viewport", {{}, assets::asset_type::menu}}};
    initial.graph.transitions = {{"open", "home", "home"}};
    initial.selection = {editor_flow::item_kind::screen, "home"};
    editor_flow::persistence graph{files.path(), initial, make_flow_storage(jobs, {})};
    flow_panel panel{graph, {}, nullptr, {}, "018d0b95cf2673c6a0d7dd8ce5d29b23"};
    const auto graph_xml = editor_flow::write_xml(graph.edit().current());
    ASSERT_TRUE(panel.execute({control_kind::flow, "control-events"}, ui));
    ASSERT_EQ(ui.selected(), selected);
    ASSERT_TRUE(panel.execute({control_kind::flow, "action"}, ui));
    ASSERT_TRUE(panel.execute({control_kind::flow, "event-route"}, ui));
    ASSERT_TRUE(panel.execute({control_kind::flow, "bind"}, ui));
    ASSERT_EQ(panel.status(),
              "Event binding updated in UI document; save that document separately");
    const auto bound = ui_content::parse_xml({ui_content::write_xml(ui.document())});
    ASSERT_EQ(bound.elements.back().events.size(), 1U);
    EXPECT_EQ(bound.elements.back().events.front().route, "open");
    EXPECT_EQ(bound.elements.back().events.front().screen, "home");
    EXPECT_EQ(editor_flow::write_xml(graph.edit().current()), graph_xml);
    ASSERT_TRUE(ui.undo());
    EXPECT_TRUE(ui.document().elements.back().events.empty());
}

/// @brief Modifier changes keep their order relative to pointer samples in the same native batch.
TEST_F(flow_workspace_test, MultiselectAndConnectPreserveModifierOrdering)
{
    editor_flow::document initial;
    initial.id = "test_flow";
    initial.graph.regions = {{"viewport", "home", ""}};
    initial.graph.screens = {{"home", "viewport", {{}, assets::asset_type::menu}},
                             {"other", "viewport", {{}, assets::asset_type::menu}}};
    initial.layout = {{{editor_flow::item_kind::screen, "home"}, "Home", 10, 20},
                      {{editor_flow::item_kind::screen, "other"}, "Other", 180, 20}};
    editor_flow::persistence graph{files.path(), initial, make_flow_storage(jobs, {})};
    editor_view view{ui, jobs, files.path().parent_path() / "ui.xml", {}};
    view.bind_flow(graph, nullptr, {}, "018d0b95cf2673c6a0d7dd8ce5d29b23");
    (void)view.snapshot(state, {});
    click(view, 820, 524);
    click(view, 40, 100);
    const std::array events{
        input::event{.device = {2, input::device_kind::keyboard}, .control = 'M', .value = {1, 0}},
        input::event{.device = {1, input::device_kind::pointer},
                     .control = input::pointer_control::primary,
                     .value = {1, 0},
                     .position = {210, 100}},
        input::event{.device = {1, input::device_kind::pointer},
                     .control = input::pointer_control::primary,
                     .position = {210, 100}},
        input::event{.device = {2, input::device_kind::keyboard}, .control = 'M'}};
    (void)view.snapshot(state, events);
    click(view, 400, 18);
    ASSERT_EQ(graph.edit().current().graph.transitions.size(), 1U);
    EXPECT_EQ(graph.edit().current().graph.transitions.front().from, "home");
    EXPECT_EQ(graph.edit().current().graph.transitions.front().to, "other");
}

/// @brief Reopening a graph tool cannot recycle a deleted route/node identity from an older tool.
TEST_F(flow_workspace_test, FreshToolScopesDoNotRecycleDeletedIdentities)
{
    editor_flow::document initial;
    initial.id = "test_flow";
    initial.graph.regions = {{"viewport", "", ""}};
    editor_flow::persistence graph{files.path(), initial, make_flow_storage(jobs, {})};
    flow_panel first{graph, {}, nullptr, {}, "018d0b95cf2673c6a0d7dd8ce5d29b23"};
    ASSERT_TRUE(first.execute({control_kind::flow, "screen"}, ui));
    ASSERT_EQ(graph.edit().current().graph.screens.size(), 1U);
    const auto previous = graph.edit().current().graph.screens.front().id;
    ASSERT_TRUE(first.execute({control_kind::flow, "delete"}, ui));
    ASSERT_TRUE(graph.edit().current().graph.screens.empty());
    flow_panel reopened{graph, {}, nullptr, {}, "018d0b95cf2673c6a0d7dd8ce5d29b24"};
    ASSERT_TRUE(reopened.execute({control_kind::flow, "screen"}, ui));
    ASSERT_EQ(graph.edit().current().graph.screens.size(), 1U);
    EXPECT_NE(graph.edit().current().graph.screens.front().id, previous);
}

/// @brief Graph storage failure blocks shutdown but must not prevent other documents' checkpoints.
TEST_F(flow_workspace_test, FailedGraphCheckpointStillProtectsUiDocuments)
{
    document_workspace documents{{storage}, make_document_storage(jobs, {})};
    const auto ui_path = files.path().parent_path() / "ui.xml";
    (void)documents.create(ui_path);
    editor_flow::document initial;
    initial.id = "test_flow";
    initial.graph.regions = {{"viewport", "home", ""}};
    initial.graph.screens = {{"home", "viewport", {{}, assets::asset_type::menu}}};
    editor_flow::storage failing{[](const auto&) -> std::optional<std::string>
                                 {
                                     return {};
                                 },
                                 [](const auto&, std::string_view)
                                 {
                                     throw std::runtime_error("Graph storage unavailable");
                                 },
                                 [](const auto&)
                                 {
                                 }};
    editor_flow::persistence graph{files.path(), initial, std::move(failing)};
    editor_view view{documents, jobs, {}};
    view.bind_flow(graph, nullptr, {}, "018d0b95cf2673c6a0d7dd8ce5d29b23");
    EXPECT_FALSE(view.preserve());
    EXPECT_TRUE(std::filesystem::exists(recovery_path(ui_path)));
    EXPECT_FALSE(std::filesystem::exists(ui_path));
}
} // namespace atlas::editor::tests
