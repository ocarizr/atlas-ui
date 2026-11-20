/** @file @brief Versioned incomplete graph drafts and strict runtime compatibility. */
#include "fixture.hpp"
#include <fstream>

namespace atlas::editor_flow::tests
{
/// @brief Missing runtime references remain round-trippable authoring data.
TEST(flow_document, round_trips_incomplete_graph_and_editor_metadata)
{
    auto draft = example();
    draft.graph.regions.front().initial = "missing";
    draft.graph.screens.front().asset = {};
    draft.graph.transitions = {{.id = "unfinished", .from = "home", .to = "missing"}};
    const auto bytes = write_xml(draft);
    const auto reopened = read_xml({bytes});
    EXPECT_EQ(write_xml(reopened), bytes);
    EXPECT_EQ(reopened.layout.front().x, 10);
    EXPECT_EQ(reopened.selection.id, "home");
    EXPECT_THROW(ui_content::validate_flow(reopened.graph), std::invalid_argument);
}

/// @brief Runtime XML contains no graph positions, labels or selection metadata.
TEST(flow_document, imports_strict_runtime_without_changing_persistent_ids)
{
    const auto source = example();
    const auto runtime = ui_content::write_flow_xml(source.graph);
    const auto imported = import_runtime("imported_flow", {runtime});
    EXPECT_EQ(ui_content::write_flow_xml(imported.graph), runtime);
    EXPECT_EQ(imported.graph.screens.front().id, "home");
    EXPECT_EQ(imported.id, "imported_flow");
}

/// @brief Data-driven nested tabs, popup overrides and all four scalar types round-trip exactly.
TEST(flow_document, round_trips_complete_settings_fixture_and_exports_runtime_only)
{
    std::ifstream file{ATLAS_FLOW_DRAFT_FIXTURE, std::ios::binary};
    ASSERT_TRUE(file);
    const std::string bytes{std::istreambuf_iterator<char>{file}, {}};
    const auto authored = read_xml({bytes});
    const auto canonical = write_xml(authored);
    EXPECT_EQ(write_xml(read_xml({canonical})), canonical);
    session editor{authored};
    const auto candidate = editor.compile();
    const auto runtime = candidate.xml();
    EXPECT_EQ(ui_content::write_flow_xml(ui_content::parse_flow_xml({runtime})), runtime);
    EXPECT_EQ(runtime.find("<layout"), std::string::npos);
    EXPECT_EQ(runtime.find("<selection"), std::string::npos);
    EXPECT_EQ(authored.selection.id, "switch_tab");
    EXPECT_EQ(authored.graph.screens.back().popup.dismiss_outside, false);
}
} // namespace atlas::editor_flow::tests
