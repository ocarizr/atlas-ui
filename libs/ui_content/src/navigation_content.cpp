/** @file @brief Validate complete navigation graphs and wire a private input candidate. */
#include "navigation_content.hpp"
#include <set>

namespace atlas::ui_content::detail
{
namespace
{
/// @brief Distinct semantic IDs remove ambiguity across active groups in a view.
void validate_actions(ui::focus_navigation_actions actions, std::set<input::action_id>& used)
{
    for (const auto id :
         {actions.left, actions.right, actions.up, actions.down, actions.next, actions.previous})
    {
        if (id && !used.insert(id).second)
        {
            throw std::invalid_argument("duplicate authored focus navigation action");
        }
    }
}
} // namespace

/// @brief Validate destination identity and author focus eligibility before runtime publication.
void validate_navigation(const document& source)
{
    if (!source.focus_input && std::ranges::none_of(source.elements,
                                                    [](const auto& node)
                                                    {
                                                        return node.focus.has_value() ||
                                                               node.focus_scope.has_value();
                                                    }))
    {
        return;
    }

    std::map<std::string_view, const element*> nodes;
    std::set<input::action_id> actions;
    if (source.focus_input)
    {
        validate_actions(source.focus_input->actions, actions);
    }

    for (const auto& node : source.elements)
    {
        nodes.emplace(node.id, &node);
    }

    for (const auto& node : source.elements)
    {
        if (node.focus_scope)
        {
            validate_actions(node.focus_scope->actions, actions);
        }

        if (!node.focus)
        {
            continue;
        }

        if (!node.interaction || !node.interaction->focusable)
        {
            throw std::invalid_argument("focus metadata needs a focusable interaction");
        }

        for (std::size_t i = 0; i < node.focus->destinations.size(); ++i)
        {
            const auto& destination = node.focus->destinations[i];
            if (destination.empty())
            {
                continue;
            }

            const auto found = nodes.find(destination);
            if (node.focus->blocked[i] || found == nodes.end())
            {
                throw std::invalid_argument("invalid authored focus destination");
            }

            const auto& interaction = found->second->interaction;
            if (!interaction || !interaction->focusable)
            {
                throw std::invalid_argument("focus destination needs a focusable interaction");
            }
        }
    }
}

/// @brief Settings resolve after all handlers exist; a failure discards the entire private
/// instance.
void configure_navigation(ui::input_session& input, const elements& ids, const document& source)
{
    if (source.focus_input)
    {
        input.configure_navigation(source.focus_input->actions, source.focus_input->keyboard);
    }

    for (const auto& node : source.elements)
    {
        if (node.focus_scope)
        {
            input.configure_navigation_scope(ids.at(node.id), *node.focus_scope);
        }

        if (!node.focus)
        {
            continue;
        }

        ui::focus_navigation_options options;
        options.blocked = node.focus->blocked;
        options.tab_order = node.focus->tab_order;
        options.tab_stop = node.focus->tab_stop;
        for (std::size_t i = 0; i < options.destinations.size(); ++i)
        {
            if (!node.focus->destinations[i].empty())
            {
                options.destinations[i] = ids.at(node.focus->destinations[i]);
            }
        }

        input.configure_navigation(ids.at(node.id), options);
    }
}
} // namespace atlas::ui_content::detail
