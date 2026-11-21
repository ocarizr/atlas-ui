/** @file @brief The native editor composes an isolated Atlas Flow preview from a typed mount. */
#include "../../../editor_flow/fixture.hpp"
#include "flow_panel.hpp"
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

namespace atlas::editor::tests
{
/// @brief Real panel commands publish runtime packets without modifying either authored document.
TEST(flow_editor_preview, AppliesGraphThroughSharedStreamerAndKeepsUiAuthoringIndependent)
{
    jobs::job_system jobs{1};
    auto source = editor_flow::tests::example();
    const auto reference = source.graph.screens.front().asset;
    const std::string xml =
        "<ui version='2' kind='menu'><element id='root'><paint fill='true'/></element></ui>";
    assets::catalog_document catalog{reference.id.space};
    (void)assets::allocate_id(catalog);
    catalog.records.push_back({reference, {"fixture", "home.xml"}, 1, xml.size()});
    streaming::storage_mount backing;
    backing.routes.push_back({reference, 1, "home.xml", 0});
    backing.sources.emplace(
        "home.xml", streaming::memory_asset{
                        std::make_shared<const streaming::asset_bytes>(xml.begin(), xml.end())});
    auto storage = std::make_shared<streaming::catalog_streamer>(
        jobs, assets::catalog::compile(catalog), backing);
    document_session ui{new_document(), {*storage}};
    const auto original = ui_content::write_xml(ui.document());
    editor_flow::tests::memory_storage files;
    editor_flow::persistence graph{"flow.xml", source, files.adapter()};
    flow_panel panel{graph, {}};
    panel.visible = true;
    panel.bind_preview(jobs.dispatcher(), storage);
    ui::layout_engine layout{jobs.dispatcher()};
    ui::paint_engine paint{jobs.dispatcher()};
    ASSERT_TRUE(panel.execute({control_kind::flow, "preview:apply"}, ui));
    panel_plan plan;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
    do
    {
        plan = {};
        plan.preview = {0, 0, 960, 500};
        panel.append(plan, layout, paint, ui);
        (void)panel.pump_preview({}, 1);
        std::this_thread::yield();
    } while ((!plan.flow_preview || plan.flow_preview->empty()) &&
             std::chrono::steady_clock::now() < deadline);

    ASSERT_TRUE(plan.flow_preview.has_value()) << panel.status();
    const auto quads = plan.flow_preview.value_or(std::vector<ui::paint_quad>{});
    ASSERT_FALSE(quads.empty()) << panel.status();
    EXPECT_EQ(quads.front().clip, (ui::layout_rect{4, 54, 704, 420}));
    EXPECT_EQ(ui_content::write_xml(ui.document()), original);
    EXPECT_TRUE(files.files.empty());
    ASSERT_TRUE(panel.execute({control_kind::flow, "preview:stop"}, ui));
    plan = {};
    plan.preview = {0, 0, 960, 500};
    panel.append(plan, layout, paint, ui);
    ASSERT_TRUE(plan.flow_preview.has_value());
    EXPECT_TRUE(plan.flow_preview.value_or(std::vector<ui::paint_quad>{}).empty());
}
} // namespace atlas::editor::tests
