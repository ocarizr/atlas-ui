/** @file @brief Resolve Flow history, visibility and tentative transition plans. */
#include "flow_state.hpp"
#include <algorithm>
#include <limits>

namespace atlas::ui_content
{
namespace
{
/// @brief Only contiguous overlay entries need their preceding views for painting.
std::size_t first_visible(const std::vector<detail::flow_entry>& stack)
{
    auto index = stack.size() - 1;
    while (index > 0 && stack.at(index).overlay)
    {
        --index;
    }

    return index;
}

/// @brief Enumerate ordered presentation and hide input behind an overlay.
void visit(const flow_document& graph, const detail::flow_stacks& state,
           std::vector<detail::flow_position>& result, std::size_t owner, bool input)
{
    const auto& stack = state.at(owner);
    if (stack.empty())
    {
        return;
    }

    for (std::size_t index = first_visible(stack); index < stack.size(); ++index)
    {
        const bool foreground = input && index + 1 == stack.size();
        result.push_back({owner, index, foreground});
        const auto& screen = graph.screens.at(stack.at(index).screen);
        for (std::size_t child = 0; child < graph.regions.size(); ++child)
        {
            if (graph.regions.at(child).owner == screen.id)
            {
                visit(graph, state, result, child, foreground);
            }
        }
    }
}
} // namespace

/// @brief Traverse only active child regions, in stable declaration order.
std::vector<detail::flow_position>
flow_manager::impl::visible(const detail::flow_stacks& state) const
{
    std::vector<detail::flow_position> result;
    visit(graph, state, result, root, true);
    return result;
}

/// @brief A newly active nested region begins at its authored default screen.
void flow_manager::impl::populate(detail::flow_stacks& state, std::size_t selected) const
{
    auto& stack = state.at(selected);
    if (stack.empty())
    {
        stack.push_back({screen(graph.regions.at(selected).initial)});
    }

    for (std::size_t index = first_visible(stack); index < stack.size(); ++index)
    {
        const auto& owner = graph.screens.at(stack.at(index).screen).id;
        for (std::size_t child = 0; child < graph.regions.size(); ++child)
        {
            if (graph.regions.at(child).owner == owner)
            {
                populate(state, child);
            }
        }
    }
}

/// @brief Resolve an authored region without retaining references across transactions.
std::size_t flow_manager::impl::region(std::string_view name) const
{
    const auto found = std::ranges::find(graph.regions, name, &flow_region::id);
    if (found == graph.regions.end())
    {
        throw std::invalid_argument("Unknown flow region");
    }

    return static_cast<std::size_t>(found - graph.regions.begin());
}

/// @brief Resolve an authored menu without retaining pointers into runtime history.
std::size_t flow_manager::impl::screen(std::string_view name) const
{
    const auto found = std::ranges::find(graph.screens, name, &flow_screen::id);
    if (found == graph.screens.end())
    {
        throw std::invalid_argument("Unknown flow screen");
    }

    return static_cast<std::size_t>(found - graph.screens.begin());
}

/// @brief Use exact typed guard equality at admission and publication.
bool flow_manager::impl::enabled(const flow_transition& edge) const
{
    if (!edge.enabled)
    {
        return false;
    }

    if (!edge.guard)
    {
        return true;
    }

    const auto found =
        std::ranges::find(graph.parameters, edge.guard->parameter, &flow_parameter::id);
    return parameters.at(static_cast<std::size_t>(found - graph.parameters.begin())) ==
           edge.guard->equals;
}

namespace
{
/// @brief Apply one checked history operation, with bounded reconstructible entries.
bool apply_operation(std::vector<detail::flow_entry>& stack, flow_operation kind,
                     std::size_t target)
{
    switch (kind)
    {
        case flow_operation::pop:
        case flow_operation::back:
            if (stack.size() < 2)
            {
                return false;
            }
            stack.pop_back();
            return true;
        case flow_operation::reload:
            stack.back().session.reset();
            stack.back().focus_pending = true;
            return true;
        case flow_operation::replace:
            if (stack.back().screen == target)
            {
                return false;
            }
            if (std::ranges::find(stack, target, &detail::flow_entry::screen) != stack.end())
            {
                return false;
            }
            stack.back() = {target, stack.back().overlay};
            return true;
        case flow_operation::push:
        case flow_operation::overlay:
            if (stack.size() >= 16 ||
                std::ranges::find(stack, target, &detail::flow_entry::screen) != stack.end())
            {
                return false;
            }
            stack.push_back({target, kind == flow_operation::overlay});
            return true;
    }

    return false;
}
} // namespace

/// @brief Resolve the source generation, guard and selected history stack.
bool flow_manager::impl::apply_transition(detail::flow_candidate& next,
                                          const detail::flow_command& command)
{
    const auto found =
        std::ranges::find(graph.transitions, command.transition, &flow_transition::id);
    if (found == graph.transitions.end() || !enabled(*found))
    {
        return false;
    }

    const auto source = screen(found->from);
    auto owner = region(graph.screens.at(source).region);
    const auto shown = visible(current);
    if (std::ranges::none_of(shown,
                             [&](const auto& position)
                             {
                                 const auto& entry = current.at(position.region).at(position.index);
                                 return position.input && entry.screen == source && entry.session &&
                                        (command.origin == 0 ||
                                         entry.session->generation == command.origin);
                             }))
    {
        return false;
    }

    if (found->operation == flow_operation::back)
    {
        while (next.stacks.at(owner).size() == 1 && owner != root)
        {
            owner = region(graph.screens.at(screen(graph.regions.at(owner).owner)).region);
        }
    }

    const auto target = found->to.empty() ? source : screen(found->to);
    if (!apply_operation(next.stacks.at(owner), found->operation, target))
    {
        return false;
    }

    next.edge = &*found;
    return true;
}

/// @brief Generate the screen's control routes and reserve a unique load generation.
void flow_manager::impl::load_view(detail::flow_candidate& next, detail::flow_position position)
{
    const auto& entry = next.stacks.at(position.region).at(position.index);
    if (sequence == std::numeric_limits<std::uint64_t>::max())
    {
        throw std::overflow_error("Flow view generations exhausted");
    }

    const auto generation = ++sequence;
    const auto& menu = graph.screens.at(entry.screen);
    flow_load_request request{menu.asset, {}, {}};
    for (const auto& edge : graph.transitions)
    {
        if (edge.from == menu.id && !edge.control.empty() && edge.enabled)
        {
            request.controls.push_back({edge.control, edge.id, edge.action});
        }
    }

    request.dispatch = [weak = std::weak_ptr{admission}, generation](std::string_view transition)
    {
        const auto active = weak.lock();
        if (transition.empty() || transition.size() > 64 || !active ||
            active->owner != std::this_thread::get_id() || active->busy || active->command ||
            !active->inputs.contains(generation))
        {
            return false;
        }

        active->command = detail::flow_command{std::string{transition}, generation};
        return true;
    };
    auto load = loader(std::move(request));
    if (!load)
    {
        throw std::runtime_error("Flow loader returned no request");
    }

    next.pending.push_back({position, generation, std::move(load)});
}

/// @brief Build the next state without changing the currently published composition.
bool flow_manager::impl::plan(const detail::flow_command& command)
{
    detail::flow_candidate next{current, {}, nullptr};
    if (!command.transition.empty() && !apply_transition(next, command))
    {
        return false;
    }

    populate(next.stacks, root);
    const auto shown = visible(next.stacks);
    if (shown.size() > 32)
    {
        return false;
    }

    for (std::size_t owner = 0; owner < next.stacks.size(); ++owner)
    {
        auto& stack = next.stacks.at(owner);
        if (std::ranges::none_of(shown,
                                 [owner](const auto& p)
                                 {
                                     return p.region == owner;
                                 }))
        {
            stack.clear();
        }
        else
        {
            for (std::size_t index = 0; index < first_visible(stack); ++index)
            {
                stack.at(index).session.reset();
            }
        }
    }

    for (const auto& position : shown)
    {
        if (!next.stacks.at(position.region).at(position.index).session)
        {
            load_view(next, position);
        }
    }

    candidate = std::move(next);
    return true;
}
} // namespace atlas::ui_content
