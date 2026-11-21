/** @file @brief Preserve bounded submenu identities and focus without retaining hidden views. */
#include "flow_state.hpp"
#include <algorithm>

namespace atlas::ui_content
{
/// @brief Input eligibility is checked before trimming a private candidate history.
bool flow_manager::impl::scope_history(std::vector<detail::flow_entry>& stack, std::size_t source)
{
    const auto origin = std::ranges::find(stack, source, &detail::flow_entry::screen);
    if (origin == stack.end())
    {
        return false;
    }

    stack.erase(std::next(origin), stack.end());
    return true;
}

/// @brief Snapshot child regions only when their parent is still composed.
std::shared_ptr<const detail::flow_bookmarks>
flow_manager::impl::bookmark(const detail::flow_stacks& state,
                             const detail::flow_entry& entry) const
{
    if (!entry.session)
    {
        return entry.children;
    }

    auto result = std::make_shared<detail::flow_bookmarks>();
    for (std::size_t child = 0; child < graph.regions.size(); ++child)
    {
        const auto& region = graph.regions.at(child);
        if (region.owner != graph.screens.at(entry.screen).id || !region.remember ||
            state.at(child).empty())
        {
            continue;
        }

        detail::flow_region_bookmark saved{child, {}};
        saved.stack.reserve(state.at(child).size());
        for (const auto& item : state.at(child))
        {
            if (item.overlay)
            {
                continue;
            }

            auto focus = item.focus;
            if (item.session)
            {
                auto& view = *item.session->view;
                const auto active = view.authored_id(view.input().focused());
                if (!active.empty())
                {
                    focus = active;
                }
            }

            saved.stack.push_back({item.screen, std::move(focus), bookmark(state, item)});
        }

        result->regions.push_back(std::move(saved));
    }

    return result->regions.empty() ? nullptr : result;
}

/// @brief Candidate copies alone change; failed loads never alter published bookmarks.
void flow_manager::impl::capture_history(detail::flow_stacks& state) const
{
    const diagnostics::operation_scope scope{diagnostics, {"ui.flow.history", "ui.content"}};
    for (auto& stack : state)
    {
        for (auto& entry : stack)
        {
            if (entry.session)
            {
                entry.children = bookmark(current, entry);
            }
        }
    }
}

/// @brief Restore remembered regions on return and defaults on a fresh menu entry.
void flow_manager::impl::populate(detail::flow_stacks& state, std::size_t selected) const
{
    auto& stack = state.at(selected);
    if (stack.empty())
    {
        stack.push_back({screen(graph.regions.at(selected).initial)});
    }

    auto first = stack.size() - 1;
    while (first > 0 && stack.at(first).overlay)
    {
        --first;
    }

    for (std::size_t index = first; index < stack.size(); ++index)
    {
        const auto& entry = stack.at(index);
        const auto& owner = graph.screens.at(entry.screen).id;
        for (std::size_t child = 0; child < graph.regions.size(); ++child)
        {
            if (graph.regions.at(child).owner != owner)
            {
                continue;
            }

            if (state.at(child).empty() && entry.children && graph.regions.at(child).remember)
            {
                const auto& saved = entry.children->regions;
                const auto found =
                    std::ranges::find(saved, child, &detail::flow_region_bookmark::region);
                if (found != saved.end())
                {
                    for (const auto& value : found->stack)
                    {
                        state.at(child).push_back(
                            {value.screen, false, {}, value.focus, true, value.children});
                    }
                }
            }

            populate(state, child);
        }
    }
}
} // namespace atlas::ui_content
