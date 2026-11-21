/** @file @brief Inspector choices use typed snapshots, graph permissions and exact scalar schemas.
 */
#include <algorithm>
#include <atlas/editor_flow/inspector.hpp>
#include <cmath>

namespace atlas::editor_flow
{
/// @brief Registry discovery returns descriptors and never executes a registered callback.
std::vector<scripting::callable_descriptor> callable_choices(const scripting::domain& scripts,
                                                             scripting::callable_kind kind)
{
    auto result = scripts.callable_descriptors();
    std::erase_if(result,
                  [&](const auto& v)
                  {
                      return v.kind != kind;
                  });
    return result;
}

/// @brief Neutral values are authored explicitly rather than relying on numeric coercion.
std::vector<mvvm::value> default_arguments(const scripting::callable_descriptor& descriptor)
{
    std::vector<mvvm::value> result;
    result.reserve(descriptor.parameters.size());
    for (const auto& parameter : descriptor.parameters)
    {
        if (parameter.default_value)
        {
            result.push_back(*parameter.default_value);
        }
        else if (!parameter.choices.empty())
        {
            result.emplace_back(parameter.choices.front().value);
        }
        else
        {
            switch (parameter.type)
            {
                case mvvm::value_type::boolean:
                    result.emplace_back(false);
                    break;
                case mvvm::value_type::integer:
                    result.emplace_back(std::int64_t{});
                    break;
                case mvvm::value_type::number:
                    result.emplace_back(0.0);
                    break;
                case mvvm::value_type::text:
                    result.emplace_back(std::string{});
                    break;
            }
        }
    }

    return result;
}

/// @brief Validate the full effective argument list before committing an inspector edit.
void validate_arguments(const scripting::callable_descriptor& descriptor,
                        const std::vector<mvvm::value>& arguments)
{
    if (arguments.size() > descriptor.parameters.size())
    {
        throw std::invalid_argument("Excess callable arguments");
    }

    for (std::size_t i = 0; i < descriptor.parameters.size(); ++i)
    {
        const auto& parameter = descriptor.parameters[i];
        if (i >= arguments.size())
        {
            if (!parameter.default_value)
            {
                throw std::invalid_argument("Missing callable argument");
            }

            continue;
        }

        const auto& value = arguments[i];
        if (mvvm::type_of(value) != parameter.type)
        {
            throw std::invalid_argument("Callable argument type mismatch");
        }

        if (const auto* text = std::get_if<std::string>(&value))
        {
            mvvm::validate_text(*text, 4096);
        }

        if (const auto* number = std::get_if<double>(&value); number && !std::isfinite(*number))
        {
            throw std::invalid_argument("Nonfinite callable argument");
        }

        if (!parameter.choices.empty() && std::ranges::none_of(parameter.choices,
                                                               [&](const auto& v)
                                                               {
                                                                   return value ==
                                                                          mvvm::value{v.value};
                                                               }))
        {
            throw std::invalid_argument("Unknown callable enum choice");
        }
    }
}

/// @brief Preserve stable declaration order and distinct outgoing route identities.
std::vector<ui_content::flow_transition> route_choices(const document& graph,
                                                       std::string_view screen)
{
    std::vector<ui_content::flow_transition> result;
    for (const auto& route : graph.graph.transitions)
    {
        if (route.from == screen)
        {
            result.push_back(route);
        }
    }

    return result;
}

/// @brief Passive leaves have no bindable input; focus events require explicit focusability.
std::vector<ui_content::authored_event> supported_events(const ui_content::element& control)
{
    if (!control.interaction)
    {
        return {};
    }

    std::vector<ui_content::authored_event> result;
    if (control.interaction->hit_test)
    {
        result.push_back(ui_content::authored_event::click);
    }

    if (control.interaction->focusable)
    {
        result.push_back(ui_content::authored_event::focus_enter);
        result.push_back(ui_content::authored_event::focus_exit);
    }

    return result;
}

/// @brief Region-owned submenu destinations cannot accidentally select reusable Widget assets.
std::vector<assets::asset_record> asset_choices(const document& graph, std::string_view region,
                                                std::span<const assets::asset_record> catalog)
{
    const auto found = std::ranges::find(graph.graph.regions, region, &ui_content::flow_region::id);
    if (found == graph.graph.regions.end())
    {
        throw std::invalid_argument("Unknown Flow region");
    }

    std::vector<assets::asset_record> result;
    for (const auto& asset : catalog)
    {
        const auto type = asset.reference.type;
        if ((!found->owner.empty() && type == assets::asset_type::submenu) ||
            (found->owner.empty() &&
             (type == assets::asset_type::menu || type == assets::asset_type::popup ||
              type == assets::asset_type::ui)))
        {
            result.push_back(asset);
        }
    }

    return result;
}

/// @brief UI-event bindings stay in UI XML; the graph remains the route permission authority.
ui_content::document bind_event(ui_content::document source, std::string_view element,
                                ui_content::event_action binding, const document& graph,
                                std::span<const scripting::callable_descriptor> registry)
{
    const auto node = std::ranges::find(source.elements, element, &ui_content::element::id);
    if (node == source.elements.end())
    {
        throw std::invalid_argument("Unknown UI event control");
    }

    const auto supported = supported_events(*node);
    if (std::ranges::find(supported, binding.event) == supported.end())
    {
        throw std::invalid_argument("Unsupported control event");
    }

    const auto screen =
        std::ranges::find(graph.graph.screens, binding.screen, &ui_content::flow_screen::id);
    if (screen == graph.graph.screens.end())
    {
        throw std::invalid_argument("Missing Flow binding context");
    }

    if (binding.action == "atlas.navigate_to")
    {
        const auto routes = route_choices(graph, binding.screen);
        const auto route =
            std::ranges::find(routes, binding.route, &ui_content::flow_transition::id);
        if (route == routes.end())
        {
            throw std::invalid_argument("UI navigation requires a graph-permitted route");
        }

        if (!binding.region.empty())
        {
            const auto region = std::ranges::find(graph.graph.regions, binding.region,
                                                  &ui_content::flow_region::id);
            const auto target =
                std::ranges::find(graph.graph.screens, route->to, &ui_content::flow_screen::id);
            if (region == graph.graph.regions.end() || region->owner.empty() ||
                region->host.empty() || screen->region != binding.region ||
                target == graph.graph.screens.end() || target->region != binding.region ||
                target->asset.type != assets::asset_type::submenu ||
                route->operation != ui_content::flow_operation::replace)
            {
                throw std::invalid_argument(
                    "Tab navigation requires its declared Submenu Area and replace route");
            }
        }
    }
    else if (binding.action != "atlas.exit_menu" && binding.action != "atlas.close_popup")
    {
        const auto found =
            std::ranges::find(registry, binding.action, &scripting::callable_descriptor::id);
        if (found == registry.end() || found->kind != scripting::callable_kind::action)
        {
            throw std::invalid_argument("Missing or incompatible registered action");
        }

        if (std::ranges::find(source.models, binding.context, &ui_content::model_context::name) ==
            source.models.end())
        {
            throw std::invalid_argument("Unknown action model context");
        }

        validate_arguments(*found, binding.arguments);
    }

    const bool navigation = binding.action == "atlas.navigate_to" ||
                            binding.action == "atlas.exit_menu" ||
                            binding.action == "atlas.close_popup";
    if (navigation && !binding.arguments.empty())
    {
        throw std::invalid_argument("Built-in navigation has no scalar arguments");
    }

    std::erase_if(node->events,
                  [&](const auto& v)
                  {
                      return v.event == binding.event && v.input_action == binding.input_action;
                  });
    node->events.push_back(std::move(binding));
    (void)ui_content::write_xml(source);
    return source;
}
} // namespace atlas::editor_flow
