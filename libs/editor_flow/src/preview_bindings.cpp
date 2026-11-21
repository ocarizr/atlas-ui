/** @file @brief Generation-bound event/condition bridges resolved privately before publication. */
#include "preview_state.hpp"
#include <algorithm>

namespace atlas::editor_flow
{
namespace
{
/// @brief A wrapped load configures a private composition before the manager can publish it.
class configured_load final : public ui_content::flow_load
{
  public:
    configured_load(std::unique_ptr<ui_content::flow_load> source,
                    std::function<void(ui_content::instance&)> prepare)
        : source_{std::move(source)}, prepare_{std::move(prepare)}
    {
        if (!source_)
        {
            throw std::invalid_argument("Flow loader returned no request");
        }
    } ///< Retain no worker access to runtime or mutable UI state.

    std::unique_ptr<ui_content::instance> poll() override
    {
        auto view = source_->poll();
        if (view)
        {
            prepare_(*view);
        }

        return view;
    } ///< Setup completes before handing the view to Flow publication.
  private:
    std::unique_ptr<ui_content::flow_load> source_;      ///< Cancelable streamer acquisition.
    std::function<void(ui_content::instance&)> prepare_; ///< Owner callback, never a job closure.
};

/// @brief Preflight the evaluator's executable subset without executing user predicates.
void executable(const authored_condition& node, std::size_t& count)
{
    if (++count > 64)
    {
        throw std::length_error("Preview condition evaluator supports 64 nodes");
    }

    if (node.callable.empty() && node.children.empty())
    {
        throw std::invalid_argument("Preview condition group must not be empty");
    }

    for (const auto& child : node.children)
    {
        executable(child, count);
    }
}

/// @brief Scope route permission to the loaded screen or its explicitly named child region.
std::string binding_screen(const ui_content::event_action& action, std::string_view owner,
                           const ui_content::flow_document& graph)
{
    if (action.screen.empty() || action.screen == owner)
    {
        return std::string{owner};
    }

    const auto region =
        std::ranges::find(graph.regions, action.region, &ui_content::flow_region::id);
    const auto target =
        std::ranges::find(graph.screens, action.screen, &ui_content::flow_screen::id);
    if (region == graph.regions.end() || region->owner != owner || region->host.empty() ||
        target == graph.screens.end() || target->region != region->id)
    {
        throw std::invalid_argument("Event action escapes its active Flow instance/region");
    }

    return target->id;
}
} // namespace

/// @brief Bind generation-checked schemas once in each actual screen's model/domain context.
void preview::impl::runtime::prepare(const std::string& screen, ui_content::instance& view)
{
    const diagnostics::operation_scope scope{sinks, {"editor.flow.preview.bind", "editor.flow"}};
    const auto& graph = candidate.graph();
    const auto definition = std::ranges::find(graph.screens, screen, &ui_content::flow_screen::id);
    if (definition == graph.screens.end())
    {
        throw std::invalid_argument("Unknown preview screen");
    }

    ui_content::validate_presentation(view.presentation(), definition->asset.type);
    if (!definition->focus.empty())
    {
        (void)view.find(definition->focus);
    }

    for (const auto& region : graph.regions)
    {
        if (region.owner != screen || region.host.empty())
        {
            continue;
        }

        const auto host = view.find(region.host);
        if (std::holds_alternative<ui::leaf_layout>(view.tree().panel(host)))
        {
            throw std::invalid_argument("Flow submenu host must be a panel");
        }

        for (auto node = host; node.generation; node = view.tree().parent(node))
        {
            const auto style = view.tree().paint(node);
            if (style.scale != ui::layout_point{1, 1} || style.rotation != 0)
            {
                throw std::invalid_argument(
                    "Flow submenu host scale/rotation requires a transformed surface adapter");
            }
        }
    }

    if (registration)
    {
        registration(view.scripts());
    }

    document source;
    source.id = "preview";
    source.graph = graph;
    for (const auto& predicate : candidate.conditions())
    {
        const auto edge = std::ranges::find(graph.transitions, predicate.transition,
                                            &ui_content::flow_transition::id);
        if (edge->from != screen)
        {
            continue;
        }

        std::size_t count = 0;
        executable(predicate.expression, count);
        source.conditions.push_back(predicate);
    }

    const auto bindings = resolve_conditions(source, view.scripts());
    guards.insert_or_assign(&view,
                            ui_content::make_flow_guard_evaluator(view.scripts(), graph, bindings));
    prepare_actions(screen, view);
}

/// @brief Compile typed UI actions separately from outgoing predicate resolution.
void preview::impl::runtime::prepare_actions(const std::string& screen, ui_content::instance& view)
{
    const auto& graph = candidate.graph();
    const auto descriptors = view.scripts().callable_descriptors();
    auto& resolved = actions[&view];
    resolved.clear();
    for (const auto& action : view.event_bindings())
    {
        const auto origin = binding_screen(action, screen, graph);
        if (action.action == "atlas.navigate_to")
        {
            const auto edge = std::ranges::find(graph.transitions, action.route,
                                                &ui_content::flow_transition::id);
            if (edge == graph.transitions.end() || edge->from != origin || !edge->enabled)
            {
                throw std::invalid_argument(
                    "Event navigation requires its exact graph-permitted route");
            }

            if (!action.region.empty())
            {
                const auto region =
                    std::ranges::find(graph.regions, action.region, &ui_content::flow_region::id);
                const auto target =
                    std::ranges::find(graph.screens, edge->to, &ui_content::flow_screen::id);
                if (region == graph.regions.end() || target == graph.screens.end() ||
                    target->region != region->id ||
                    target->asset.type != assets::asset_type::submenu ||
                    edge->operation != ui_content::flow_operation::replace)
                {
                    throw std::invalid_argument(
                        "Tab action requires a same-region submenu replace route");
                }
            }
        }
        else if (action.action != "atlas.exit_menu" && action.action != "atlas.close_popup")
        {
            const auto entry =
                std::ranges::find(descriptors, action.action, &scripting::callable_descriptor::id);
            if (entry == descriptors.end() || entry->kind != scripting::callable_kind::action)
            {
                throw std::invalid_argument("Missing preview action descriptor: " + action.action);
            }

            validate_arguments(*entry, action.arguments);
            resolved[{action.action, action.context}] =
                view.scripts().bind_callable(action.action, action.context);
            continue;
        }

        if (!action.arguments.empty())
        {
            throw std::invalid_argument("Built-in navigation has no scalar arguments");
        }
    }
}

/// @brief Execute callbacks only in the originating UI frame; built-ins stage navigation demands.
void preview::impl::runtime::dispatch(const std::string& screen, ui_content::instance& view,
                                      const ui_content::event_action& action)
{
    if (!flow || suppress_actions)
    {
        return;
    } // Private preflight never runs user input.

    const auto source_view = live.find(screen);
    if (source_view == live.end() || source_view->second != &view || !eligible.contains(screen))
    {
        return;
    }

    const auto origin = binding_screen(action, screen, candidate.graph());
    if (!eligible.contains(origin))
    {
        return;
    } // A tab declaration for an inactive submenu is not a live route origin.

    if (action.action == "atlas.navigate_to" || action.action == "atlas.exit_menu" ||
        action.action == "atlas.close_popup")
    {
        if (!navigation)
        {
            navigation = action;
            navigation->screen = origin;
        }
    }
    else
    {
        view.scripts().invoke_action(actions.at(&view).at({action.action, action.context}),
                                     action.arguments);
    }
}

/// @brief Revalidate outgoing conditions against the current source, including publication time.
bool preview::impl::runtime::guard(const ui_content::flow_transition& edge)
{
    if (!flow)
    {
        throw std::logic_error("Guard requires a live preview manager");
    }

    if (const auto item = live.find(edge.from); item != live.end())
    {
        const bool result = guards.at(item->second)(edge);
        if (evaluated.size() == 64)
        {
            evaluated.erase(evaluated.begin());
        }

        evaluated.emplace_back(edge.id, result);
        return result;
    }

    throw std::logic_error("Guard source is not actively presented");
}

/// @brief Each instance retains its own endpoint even when one asset appears in several nodes.
ui_content::flow_loader preview::impl::runtime::factory()
{
    return [this](ui_content::flow_load_request request)
    {
        auto endpoint = std::make_shared<ui_content::instance*>(nullptr);
        auto screen = request.screen;
        request.dispatch_event = [this, screen, endpoint](const ui_content::event_action& action)
        {
            if (*endpoint)
            {
                dispatch(screen, **endpoint, action);
            }
        };
        return std::make_unique<configured_load>(
            loader(std::move(request)),
            [this, screen, endpoint](ui_content::instance& view)
            {
                prepare(screen, view);
                *endpoint = &view;
            });
    };
}
} // namespace atlas::editor_flow
