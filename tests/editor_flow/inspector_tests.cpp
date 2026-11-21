/** @file @brief Typed event/action and outgoing condition authoring without source scanning. */
#include "fixture.hpp"
#include <atlas/editor_flow/inspector.hpp>

namespace atlas::editor_flow::tests
{
/// @brief Engine namespace actions retain typed parameters just like project-defined actions.
TEST(flow_inspector, EngineActionsAreNotMistakenForNavigationBuiltins)
{
    auto graph = example();
    ui_content::document ui;
    auto model = graph.graph.screens.front().asset;
    model.type = assets::asset_type::view_model;
    ui.models.push_back({"state", model});
    ui_content::element button;
    button.id = "button";
    button.interaction = ui::interaction_options{};
    ui.elements.push_back(button);
    const std::array registry{
        scripting::callable_descriptor{"atlas.set_theme",
                                       "Theme",
                                       "Editor",
                                       {{"theme", mvvm::value_type::integer, std::int64_t{1}}}}};
    ui_content::event_action binding;
    binding.action = "atlas.set_theme";
    binding.context = "state";
    binding.screen = "home";
    binding.arguments = {std::int64_t{1}};
    EXPECT_EQ(bind_event(ui, "button", binding, graph, registry)
                  .elements.front()
                  .events.front()
                  .arguments,
              binding.arguments);
}

/// @brief Choice discovery separates actions from predicates and retains distinct edge IDs.
TEST(flow_inspector, ShowsOnlyCompatibleRegistryEntriesAndRouteIdentities)
{
    scripting::domain scripts;
    scripts.register_action(
        {"project"},
        {"project.buy", "Buy", "Store", {{"amount", mvvm::value_type::integer, std::int64_t{1}}}},
        [](auto&, auto)
        {
        });
    scripts.register_condition({"project"}, {"project.allowed", "Allowed", "Store"},
                               [](const auto&, auto)
                               {
                                   return true;
                               });
    const auto actions = callable_choices(scripts, scripting::callable_kind::action);
    ASSERT_EQ(actions.size(), 1U);
    EXPECT_EQ(actions.front().id, "project.buy");
    EXPECT_EQ(default_arguments(actions.front()), std::vector<mvvm::value>{std::int64_t{1}});
    EXPECT_THROW(validate_arguments(actions.front(), {mvvm::value{1.0}}), std::invalid_argument);
    auto source = example();
    source.graph.transitions = {{"one", "home", "home"}, {"two", "home", "home"}};
    EXPECT_EQ(route_choices(source, "home").size(), 2U);
    EXPECT_TRUE(route_choices(source, "missing").empty());
}

/// @brief A binding roundtrip preserves its UI owner and rejects a route outside graph permissions.
TEST(flow_inspector, EventBindingStaysInUiXmlAndTargetsOnlyPermittedRoutes)
{
    auto graph = example();
    graph.graph.transitions = {{"open", "home", "home"}};
    ui_content::document ui;
    ui_content::element button;
    button.id = "button";
    button.interaction = ui::interaction_options{.focusable = true};
    ui.elements.push_back(button);
    ui_content::event_action binding;
    binding.action = "atlas.navigate_to";
    binding.route = "open";
    binding.screen = "home";
    auto edited = bind_event(ui, "button", binding, graph, {});
    const auto xml = ui_content::write_xml(edited);
    const auto roundtrip = ui_content::parse_xml({xml});
    ASSERT_EQ(roundtrip.elements.front().events.size(), 1U);
    EXPECT_EQ(roundtrip.elements.front().events.front().route, "open");
    EXPECT_TRUE(ui.elements.front().events.empty());
    binding.route = "missing";
    EXPECT_THROW((void)bind_event(ui, "button", binding, graph, {}), std::invalid_argument);
    EXPECT_TRUE(supported_events(button).size() >= 3);
}

/// @brief Missing predicate implementations remain saveable but never resolve into permission.
TEST(flow_inspector, ConditionsRoundtripAndResolveOnlyOnExplicitCompile)
{
    auto source = example();
    source.graph.transitions = {{"open", "home", "home"}};
    authored_condition leaf;
    leaf.callable = "project.allowed";
    leaf.context = "state";
    leaf.negate = true;
    source.conditions.push_back({"open", {{}, {}, {}, {leaf}, true}});
    const auto xml = write_xml(source);
    const auto restored = read_xml({xml});
    ASSERT_EQ(restored.conditions.size(), 1U);
    EXPECT_TRUE(restored.conditions.front().expression.negate);
    EXPECT_EQ(restored.conditions.front().expression.children.front().callable, "project.allowed");
    scripting::domain scripts;
    EXPECT_THROW((void)resolve_conditions(restored, scripts), std::invalid_argument);
    EXPECT_EQ(read_xml({xml}).conditions.size(), 1U);
}
} // namespace atlas::editor_flow::tests
