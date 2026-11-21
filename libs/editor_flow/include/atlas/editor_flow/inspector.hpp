/**
 * @file
 * @brief Typed asset, route, action and condition choices for shared authoring inspectors.
 */
#pragma once
#include <atlas/editor_flow/document.hpp>
#include <atlas/ui_content/flow_conditions.hpp>

namespace atlas::editor_flow
{
/// @brief Filter a registered metadata snapshot without invoking native or Lua behavior.
[[nodiscard]] std::vector<scripting::callable_descriptor>
callable_choices(const scripting::domain& scripts, scripting::callable_kind kind);
/// @brief Materialize explicit defaults; required parameters receive their exact neutral scalar.
[[nodiscard]] std::vector<mvvm::value>
default_arguments(const scripting::callable_descriptor& descriptor);
/// @brief Reject arity/type/enum mismatches before creating a binding.
void validate_arguments(const scripting::callable_descriptor& descriptor,
                        const std::vector<mvvm::value>& arguments);
/// @brief Only explicit outgoing routes appear, including distinct routes to one destination.
[[nodiscard]] std::vector<ui_content::flow_transition> route_choices(const document& graph,
                                                                     std::string_view screen);
/// @brief Reusable controls expose only their declared input and focus capabilities.
[[nodiscard]] std::vector<ui_content::authored_event>
supported_events(const ui_content::element& control);
/// @brief Menu-owned regions list only the compatible explicit Submenu assets from a catalog.
[[nodiscard]] std::vector<assets::asset_record>
asset_choices(const document& graph, std::string_view region,
              std::span<const assets::asset_record> catalog);
/// @brief Validate and replace the selected event binding in an independent UI-asset document.
[[nodiscard]] ui_content::document
bind_event(ui_content::document source, std::string_view element, ui_content::event_action binding,
           const document& graph, std::span<const scripting::callable_descriptor> registry);
/// @brief Bind persistent predicates to exact registry generations only at explicit preview apply.
[[nodiscard]] std::vector<ui_content::flow_condition_binding>
resolve_conditions(const document& source, scripting::domain& scripts);
/// @brief Bound syntax validation allows missing registrations in saveable drafts.
void validate_conditions(const document& source);
} // namespace atlas::editor_flow
