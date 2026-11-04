/** @file @brief Immutable identity validation and deterministic sibling stacking links. */
#include "paint_state.hpp"
#include <algorithm>
#include <numeric>

namespace atlas::ui::detail
{
/// @brief Verify immutable source/geometry identity before indexing either snapshot.
std::size_t validate_paint_geometry(const element_snapshot& input, const layout_snapshot& geometry)
{
    if (input.session() != geometry.session() || input.revision() != geometry.revision() ||
        input.viewport() != geometry.viewport() ||
        input.elements().size() != geometry.items().size())
    {
        throw std::invalid_argument("UI paint requires matching layout snapshot");
    }

    const auto nodes = input.elements();
    const auto layouts = geometry.items();
    std::size_t slots = 0;
    for (std::size_t i = 0; i < nodes.size(); ++i)
    {
        if (nodes[i].target != layouts[i].target)
        {
            throw std::invalid_argument("UI paint geometry identity mismatch");
        }

        slots = std::max(slots, std::size_t{nodes[i].target.index} + 1);
    }

    return slots;
}

/// @brief Sibling ranks build isolated stable painter links without per-element child vectors.
void order_paint(std::span<const element_record> nodes, std::size_t slots,
                 std::pmr::memory_resource* memory, paint_links& links)
{
    const auto none = nodes.size();
    /// @brief Direct immutable slot-to-record lookup for hierarchy traversal.
    std::pmr::vector<std::size_t> map(slots, none, memory);
    auto& next = links.next;
    auto& first = links.first;
    std::pmr::vector<std::size_t> order(nodes.size(), memory);
    std::pmr::vector<std::size_t> rank(nodes.size(), memory);
    std::iota(order.begin(), order.end(), std::size_t{0});
    for (std::size_t i = 0; i < nodes.size(); ++i)
    {
        map[nodes[i].target.index] = i;
    }

    const auto index = [&](element_handle h)
    {
        return h.generation == 0 ? none : map.at(h.index);
    };
    std::size_t ordinal = 0;
    for (std::size_t i = 0; i < nodes.size(); ++i)
    {
        if (nodes[i].parent.generation == 0)
        {
            rank[i] = ordinal++;
        }

        auto child = index(nodes[i].first_child);
        while (child != none)
        {
            rank[child] = ordinal++;
            child = index(nodes[child].next_sibling);
        }
    }

    std::sort(order.begin(), order.end(),
              [&](std::size_t a, std::size_t b)
              {
                  const auto pa = index(nodes[a].parent);
                  const auto pb = index(nodes[b].parent);
                  if (pa != pb)
                  {
                      return pa < pb;
                  }

                  if (nodes[a].paint.layer != nodes[b].paint.layer)
                  {
                      return nodes[a].paint.layer > nodes[b].paint.layer;
                  }

                  return rank[a] < rank[b];
              });
    for (auto it = order.rbegin(); it != order.rend(); ++it)
    {
        const auto parent = index(nodes[*it].parent);
        next[*it] = first[parent];
        first[parent] = *it;
    }
}
} // namespace atlas::ui::detail
