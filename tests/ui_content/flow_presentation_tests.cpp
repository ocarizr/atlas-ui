/** @file @brief Authored typed menu regions, popup overrides and prepared compatibility. */
#include <atlas/ui_content/flow.hpp>
#include <atlas/ui_content/prepared.hpp>
#include <gtest/gtest.h>

namespace atlas::ui_content::tests
{
namespace
{
/// @brief A fixed typed graph with root menus, two tabs and one popup.
flow_document typed_graph()
{
    const auto ref = [](std::uint64_t id, assets::asset_type type)
    {
        return assets::asset_ref{{assets::parse_namespace("018d0b95cf2673c6a0d7dd8ce5d29b23"), id},
                                 type};
    };
    flow_document result;
    result.regions = {{"main", "home", ""}, {"tabs", "audio", "settings", "content", true}};
    result.screens = {{"home", "main", ref(1, assets::asset_type::menu)},
                      {"settings", "main", ref(2, assets::asset_type::menu)},
                      {"audio", "tabs", ref(3, assets::asset_type::submenu)},
                      {"video", "tabs", ref(4, assets::asset_type::submenu)},
                      {"dialog", "main", ref(5, assets::asset_type::popup)}};
    result.screens.back().popup.modal = false;
    result.screens.back().popup.dismiss_outside = true;
    result.transitions = {
        {.id = "open", .from = "home", .to = "settings"},
        {.id = "tab", .from = "audio", .to = "video", .operation = flow_operation::replace},
        {.id = "show", .from = "settings", .to = "dialog", .operation = flow_operation::overlay},
        {.id = "close", .from = "dialog", .operation = flow_operation::close_popup},
        {.id = "exit", .from = "settings", .operation = flow_operation::exit_menu}};
    return result;
}
} // namespace

/// @brief UI kinds, submenu hosts and optional policies survive both source and prepared records.
TEST(flow_presentation, RoundTripsTypedGraphAndPreparedPayload)
{
    const auto graph = typed_graph();
    const auto xml = write_flow_xml(graph);
    EXPECT_NE(xml.find("version=\"2\""), std::string::npos);
    EXPECT_EQ(write_flow_xml(parse_flow_xml({xml})), xml);
    EXPECT_EQ(parse_flow_xml({xml}).regions.back().host, "content");
    const assets::asset_ref flow{{graph.screens.front().asset.id.space, 6},
                                 assets::asset_type::flow};
    prepared_flow prepared{{flow, 1}, graph, {}};
    for (const auto& screen : graph.screens)
    {
        prepared.screens.push_back({screen.asset, {{screen.asset, 1}}});
    }

    EXPECT_EQ(write_flow_xml(decode_prepared_flow(encode_prepared(prepared)).graph), xml);
}

/// @brief A reusable widget is not a destination and a submenu requires an owned panel host.
TEST(flow_presentation, RejectsIncompatibleRolesAndMissingRegionHosts)
{
    auto graph = typed_graph();
    graph.screens.front().asset.type = assets::asset_type::widget;
    EXPECT_THROW(validate_flow(graph), std::invalid_argument);
    graph = typed_graph();
    graph.regions.back().host.clear();
    EXPECT_THROW(validate_flow(graph), std::invalid_argument);
    graph = typed_graph();
    graph.screens.at(2).asset.type = assets::asset_type::menu;
    EXPECT_THROW(validate_flow(graph), std::invalid_argument);
}

/// @brief Explicit popups cannot replace the region's baseline or open as ordinary menus.
TEST(flow_presentation, RejectsPopupAsInitialOrRegularPush)
{
    auto graph = typed_graph();
    graph.regions.front().initial = "dialog";
    EXPECT_THROW(validate_flow(graph), std::invalid_argument);
    graph = typed_graph();
    graph.transitions.at(2).operation = flow_operation::push;
    EXPECT_THROW(validate_flow(graph), std::invalid_argument);
}

/// @brief An explicit false override is distinct from inheriting a true asset default.
TEST(flow_presentation, ResolvesOnlyExplicitPopupOverrides)
{
    const popup_policy defaults{true, true, false};
    popup_overrides overrides;
    overrides.modal = false;
    const auto resolved = resolve_popup(defaults, overrides);
    EXPECT_FALSE(resolved.modal);
    EXPECT_TRUE(resolved.dismiss_outside);
    EXPECT_FALSE(resolved.dismiss_back);
    EXPECT_EQ(defaults, (popup_policy{true, true, false}));
}
} // namespace atlas::ui_content::tests
