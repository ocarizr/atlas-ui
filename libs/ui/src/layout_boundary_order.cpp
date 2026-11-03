/** @file @brief Stable bounded Kahn ordering for immutable sibling dependency edges. */
#include "boundary_validation.hpp"
#include "layout_state.hpp"
#include <algorithm>
#include <functional>

namespace atlas::ui::detail
{
/// @brief Preserve insertion priority among ready siblings without changing preorder storage.
void order_boundary_children(layout_source& source)
{
    if (source.boundaries.empty())
    {
        return;
    }

    auto* memory = source.memory.get();
    std::pmr::vector<std::uint32_t> degree(source.nodes.size(), 0, memory);
    std::pmr::vector<std::uint32_t> head(source.nodes.size(), no_layout_node, memory);
    std::pmr::vector<std::uint32_t> next(source.boundaries.size(), no_layout_node, memory);
    std::pmr::vector<std::uint32_t> ready(0, memory);
    for (std::uint32_t index = 0; index < source.boundaries.size(); ++index)
    {
        const auto& edge = source.boundaries[index];
        if (edge.target >= source.nodes.size() || edge.source >= source.nodes.size())
        {
            throw std::invalid_argument("invalid resolved UI boundary identity");
        }

        const auto& target = source.nodes[edge.target];
        const auto& dependency = source.nodes[edge.source];
        const auto parent = target.parent;
        const auto* canvas = parent == no_layout_node
                                 ? nullptr
                                 : std::get_if<canvas_layout>(&source.nodes[parent].panel);
        if (dependency.target != edge.policy.source || dependency.parent != parent ||
            edge.target == edge.source || !canvas || canvas->measure != canvas_measure::intrinsic)
        {
            throw std::invalid_argument("invalid resolved UI boundary scope");
        }

        layout_boundaries value;
        (edge.horizontal ? value.horizontal : value.vertical) = edge.policy;
        validate_boundary_values(value, placement(source, target));
        ++degree[edge.target];
        next[index] = head[edge.source];
        head[edge.source] = index;
    }

    for (auto& parent : source.nodes)
    {
        if (!std::holds_alternative<canvas_layout>(parent.panel))
        {
            continue;
        }

        ready.clear();
        std::size_t count = 0;
        for (auto child = parent.first; child != no_layout_node; child = source.nodes[child].next)
        {
            ++count;
            if (degree[child] == 0)
            {
                ready.push_back(child);
            }
        }

        std::make_heap(ready.begin(), ready.end(), std::greater<>{});
        std::uint32_t last = no_layout_node;
        std::size_t resolved = 0;
        while (!ready.empty())
        {
            std::pop_heap(ready.begin(), ready.end(), std::greater<>{});
            const auto child = ready.back();
            ready.pop_back();
            if (last == no_layout_node)
            {
                parent.first = child;
            }
            else
            {
                source.nodes[last].next = child;
            }

            last = child;
            ++resolved;
            for (auto edge = head[child]; edge != no_layout_node; edge = next[edge])
            {
                const auto target = source.boundaries[edge].target;
                if (--degree[target] == 0)
                {
                    ready.push_back(target);
                    std::push_heap(ready.begin(), ready.end(), std::greater<>{});
                }
            }
        }

        if (resolved != count)
        {
            throw std::invalid_argument("UI boundary dependency cycle");
        }

        if (last != no_layout_node)
        {
            source.nodes[last].next = no_layout_node;
        }
    }
}
} // namespace atlas::ui::detail
