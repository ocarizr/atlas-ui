/** @file @brief Typed event binding commands stay independent from graph mutation. */
#include "flow_panel.hpp"
#include <algorithm>
#include <charconv>

namespace atlas::editor
{
namespace
{
/// @brief Binding edits preserve the selected control rather than using recovery's root selection.
void commit_binding(document_session& ui, ui_content::document source)
{
    const auto selected = std::string{ui.selected()};
    ui.restore(std::move(source));
    ui.select(selected);
}
} // namespace

/// @brief Commit or remove UI-asset-owned bindings without dirtying the graph.
bool flow_panel::binding_command(std::string_view action, document_session& ui)
{
    const auto selection = tool_.selection();
    if (action == "bind")
    {
        if (selection.empty() || selection.front().kind != editor_flow::item_kind::screen)
        {
            throw std::invalid_argument("Select the event's source screen");
        }

        binding_.screen = selection.front().id;
        commit_binding(ui, editor_flow::bind_event(ui.document(), ui.selected(), binding_,
                                                   document_.edit().current(), descriptors_));
        status_ = "Event binding updated in UI document; save that document separately";
    }
    else if (action == "control-events")
    {
        auto source = ui.document();
        const auto node =
            std::ranges::find(source.elements, ui.selected(), &ui_content::element::id);
        if (node == source.elements.end())
        {
            throw std::invalid_argument("Select a UI control first");
        }

        node->interaction = ui::interaction_options{.focusable = true};
        commit_binding(ui, std::move(source));
    }
    else if (action.starts_with("event-edit:") || action.starts_with("event-remove:"))
    {
        const auto offset = action.find(':') + 1;
        std::size_t index{};
        const auto parsed =
            std::from_chars(action.data() + offset, action.data() + action.size(), index);
        if (parsed.ec != std::errc{} || parsed.ptr != action.data() + action.size())
        {
            throw std::invalid_argument("Invalid event index");
        }

        auto source = ui.document();
        const auto node =
            std::ranges::find(source.elements, ui.selected(), &ui_content::element::id);
        if (node == source.elements.end() || index >= node->events.size())
        {
            throw std::invalid_argument("Event binding no longer exists");
        }

        if (action.starts_with("event-edit:"))
        {
            binding_ = node->events[index];
        }
        else
        {
            node->events.erase(node->events.begin() + static_cast<std::ptrdiff_t>(index));
            commit_binding(ui, std::move(source));
        }
    }
    else
    {
        return false;
    }

    return true;
}

/// @brief Cycle only registered descriptors and graph-permitted route identities.
bool flow_panel::choice_command(std::string_view action)
{
    const auto selection = tool_.selection();
    if (action == "event")
    {
        binding_.event = static_cast<ui_content::authored_event>(
            (static_cast<unsigned>(binding_.event) + 1) % 3);
    }
    else if (action == "action")
    {
        std::vector<std::string> ids{"atlas.navigate_to", "atlas.exit_menu", "atlas.close_popup"};
        for (const auto& entry : descriptors_)
        {
            if (entry.kind == scripting::callable_kind::action)
            {
                ids.push_back(entry.id);
            }
        }

        auto found = std::ranges::find(ids, binding_.action);
        if (found != ids.end())
        {
            ++found;
        }

        if (found == ids.end())
        {
            found = ids.begin();
        }

        binding_.action = *found;
        binding_.arguments.clear();
        const auto descriptor =
            std::ranges::find(descriptors_, *found, &scripting::callable_descriptor::id);
        if (descriptor != descriptors_.end())
        {
            binding_.arguments = editor_flow::default_arguments(*descriptor);
        }
    }
    else if (action == "event-route")
    {
        if (selection.empty())
        {
            throw std::invalid_argument("Select a source screen");
        }

        const auto routes = tool_.routes(selection.front().id);
        if (routes.empty())
        {
            throw std::invalid_argument("Connect an outgoing route first");
        }

        auto found = std::ranges::find(routes, binding_.route, &ui_content::flow_transition::id);
        if (found != routes.end())
        {
            ++found;
        }

        if (found == routes.end())
        {
            found = routes.begin();
        }

        binding_.route = found->id;
    }
    else if (action == "predicate")
    {
        const auto choices =
            editor_flow::callable_choices(registry_, scripting::callable_kind::condition);
        if (choices.empty())
        {
            throw std::invalid_argument("Load a callable module before choosing conditions");
        }

        auto found = std::ranges::find(choices, predicate_, &scripting::callable_descriptor::id);
        if (found != choices.end())
        {
            ++found;
        }

        if (found == choices.end())
        {
            found = choices.begin();
        }

        predicate_ = found->id;
    }
    else
    {
        return false;
    }

    return true;
}
} // namespace atlas::editor
