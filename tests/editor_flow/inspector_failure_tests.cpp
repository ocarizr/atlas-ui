/** @file @brief Inspector boundaries reject incompatible schemas and preserve incomplete drafts. */
#include "fixture.hpp"
#include <atlas/editor_flow/inspector.hpp>

namespace atlas::editor_flow::tests
{
/// @brief Tab controls cannot target another menu or an unrelated region.
TEST(flow_inspector, TabBindingsRequireTheirDeclaredSubmenuArea)
{
    auto graph = example();
    graph.graph.regions.push_back({"tabs", "audio", "home", "content"});
    auto ref = graph.graph.screens.front().asset;
    ref.type = assets::asset_type::submenu;
    graph.graph.screens.push_back({"audio", "tabs", ref});
    graph.graph.screens.push_back({"video", "tabs", ref});
    graph.graph.transitions = {{"switch", "audio", "video", ui_content::flow_operation::replace}};
    ui_content::document ui;
    ui_content::element button;
    button.id = "tab";
    button.interaction = ui::interaction_options{.focusable = true};
    ui.elements.push_back(button);
    ui_content::event_action binding;
    binding.action = "atlas.navigate_to";
    binding.screen = "audio";
    binding.route = "switch";
    binding.region = "tabs";
    EXPECT_EQ(bind_event(ui, "tab", binding, graph, {}).elements.front().events.front().region,
              "tabs");
    binding.region = "viewport";
    EXPECT_THROW((void)bind_event(ui, "tab", binding, graph, {}), std::invalid_argument);
    binding.region = "tabs";
    graph.graph.transitions.front().operation = ui_content::flow_operation::push;
    EXPECT_THROW((void)bind_event(ui, "tab", binding, graph, {}), std::invalid_argument);
}

/// @brief Type-filtered selection excludes unrelated media and reusable widgets.
TEST(flow_inspector, FiltersDestinationAssetsByDeclaredRegionOwnership)
{
    auto source = example();
    source.graph.regions.push_back({"tabs", "", "home", "content"});
    std::vector<assets::asset_record> catalog;
    for (const auto type : {assets::asset_type::menu, assets::asset_type::submenu,
                            assets::asset_type::widget, assets::asset_type::texture})
    {
        assets::asset_record record;
        record.reference = example().graph.screens.front().asset;
        record.reference.type = type;
        catalog.push_back(record);
    }

    const auto roots = asset_choices(source, "viewport", catalog);
    const auto tabs = asset_choices(source, "tabs", catalog);
    ASSERT_EQ(roots.size(), 1U);
    ASSERT_EQ(tabs.size(), 1U);
    EXPECT_EQ(roots.front().reference.type, assets::asset_type::menu);
    EXPECT_EQ(tabs.front().reference.type, assets::asset_type::submenu);
    EXPECT_THROW((void)asset_choices(source, "missing", catalog), std::invalid_argument);
}

/// @brief Unknown events and registry entries cannot silently create an unusable binding.
TEST(flow_inspector, RejectsUnsupportedEventAndUnknownCallableWithoutEditingSource)
{
    auto graph = example();
    ui_content::document source;
    ui_content::element button;
    button.id = "button";
    button.interaction = ui::interaction_options{};
    source.elements.push_back(button);
    const auto previous = ui_content::write_xml(source);
    ui_content::event_action binding;
    binding.screen = "home";
    binding.action = "project.missing";
    binding.event = ui_content::authored_event::focus_enter;
    EXPECT_THROW((void)bind_event(source, "button", binding, graph, {}), std::invalid_argument);
    binding.event = ui_content::authored_event::click;
    EXPECT_THROW((void)bind_event(source, "button", binding, graph, {}), std::invalid_argument);
    EXPECT_EQ(ui_content::write_xml(source), previous);
}

/// @brief Duplicate decorators and excessive recursion fail before allocating a recovery snapshot.
TEST(flow_inspector, BoundsConditionTreesAndPreservesCandidatePredicateSnapshot)
{
    auto source = example();
    source.graph.transitions = {{"open", "home", "", ui_content::flow_operation::reload}};
    source.conditions = {{"open", {"project.allowed", "state", {}, {}, true}}};
    session owner{source};
    const auto candidate = owner.compile();
    source.conditions.front().expression.negate = false;
    owner.apply(source);
    EXPECT_TRUE(candidate.conditions().front().expression.negate);
    source.conditions.push_back(source.conditions.front());
    EXPECT_THROW(validate(source), std::invalid_argument);
    source.conditions.resize(1);
    source.conditions.front().expression = {};
    auto* node = &source.conditions.front().expression;
    for (unsigned i = 0; i < 9; ++i)
    {
        node = &node->children.emplace_back();
    }

    EXPECT_THROW(validate(source), std::length_error);
}

/// @brief Persistent native/Lua predicate IDs bind to generations only during explicit application.
TEST(flow_inspector, ResolvesAndEvaluatesNegatedGroupsAndRejectsRetiredPredicates)
{
    auto source = example();
    source.graph.transitions = {{"open", "home", "home"}};
    source.conditions = {{"open", {{}, {}, {}, {{"project.allowed", "state"}}, true}}};
    auto model = std::make_shared<mvvm::view_model>(mvvm::compile({"state"}));
    scripting::domain scripts;
    scripts.bind_context("state", model);
    scripts.register_condition({"project"}, {"project.allowed", "Allowed", "Flow"},
                               [](const auto&, auto)
                               {
                                   return false;
                               });
    const auto resolved = resolve_conditions(source, scripts);
    ASSERT_EQ(resolved.size(), 1U);
    model->begin_frame(1);
    scripts.begin_frame(1);
    EXPECT_TRUE(scripts.evaluate_conditions(resolved.front().expression).allowed());
    scripts.remove_callable("project.allowed");
    EXPECT_FALSE(scripts.evaluate_conditions(resolved.front().expression).allowed());
}
} // namespace atlas::editor_flow::tests
