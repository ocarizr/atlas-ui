/** @file @brief Iterative immutable forest preparation with exact geometry-input copies. */
#include "layout_state.hpp"
#include <algorithm>

namespace atlas::ui::detail
{
namespace
{
/// @brief Index captured identities once, before following any relationship.
std::pmr::vector<std::uint32_t> map_records(const element_snapshot& snapshot,
                                            std::pmr::memory_resource* memory)
{
    const auto elements = snapshot.elements();
    std::size_t slots = 0;
    for (const auto& element : elements)
    {
        slots = std::max(slots, std::size_t{element.target.index} + 1);
    }

    if (slots > 65536)
    {
        throw std::invalid_argument("invalid layout snapshot slot range");
    }

    auto records = std::pmr::vector<std::uint32_t>(slots, no_layout_node, memory);
    for (std::uint32_t index = 0; index < elements.size(); ++index)
    {
        const auto target = elements[index].target;
        if (target.session != snapshot.session() || target.generation == 0 ||
            records[target.index] != no_layout_node)
        {
            throw std::invalid_argument("invalid layout snapshot identity");
        }

        records[target.index] = index;
    }

    return records;
}

/// @brief Copy only geometry inputs and sparse placement overrides into one independent branch.
template <class Link>
void append_nodes(layout_source& source, const element_snapshot& snapshot,
                  std::span<const std::uint32_t> order, const Link& link)
{
    const auto elements = snapshot.elements();
    source.nodes.reserve(order.size());
    for (const auto index : order)
    {
        const auto& value = elements[index];
        auto slot_index = no_layout_node;
        const auto overrides = snapshot.canvas_slots();
        const auto slot = std::lower_bound(overrides.begin(), overrides.end(), value.target.index,
                                           [](const canvas_slot_record& entry, std::uint32_t slot)
                                           {
                                               return entry.target.index < slot;
                                           });
        if (slot != overrides.end() && slot->target == value.target)
        {
            slot_index = static_cast<std::uint32_t>(source.slots.size());
            source.slots.push_back(slot->slot);
        }

        const auto stack_overrides = snapshot.stack_slots();
        const auto stack =
            std::lower_bound(stack_overrides.begin(), stack_overrides.end(), value.target.index,
                             [](const stack_slot_record& entry, std::uint32_t index)
                             {
                                 return entry.target.index < index;
                             });
        if (stack != stack_overrides.end() && stack->target == value.target)
        {
            slot_index = static_cast<std::uint32_t>(source.stack_slots.size());
            source.stack_slots.push_back(stack->slot);
        }

        source.nodes.push_back({value.target, link(value.parent), link(value.first_child),
                                link(value.next_sibling), value.panel, slot_index, value.layout,
                                value.requested.width, value.requested.height,
                                value.requested.width_fixed, value.requested.height_fixed});
    }
}
} // namespace

/// @brief Only a tree-created snapshot can supply input; validate identities before local indexing.
layout_forest prepare_layout(const element_snapshot& snapshot,
                             const std::shared_ptr<std::pmr::memory_resource>& memory)
{
    layout_forest result{memory.get()};
    const auto elements = snapshot.elements();
    result.records = map_records(snapshot, memory.get());
    const auto lookup = [&](element_handle handle)
    {
        if (handle == element_handle{})
        {
            return no_layout_node;
        }

        if (handle.index >= result.records.size() ||
            result.records[handle.index] == no_layout_node ||
            elements[result.records[handle.index]].target != handle)
        {
            throw std::invalid_argument("invalid layout snapshot relationship");
        }

        return result.records[handle.index];
    };
    std::pmr::vector<std::uint32_t> local(elements.size(), no_layout_node, memory.get());
    std::pmr::vector<unsigned char> visited(elements.size(), 0, memory.get());
    std::pmr::vector<std::uint32_t> order(0, memory.get());
    order.reserve(elements.size());
    std::size_t total = 0;
    for (std::uint32_t root = 0; root < elements.size(); ++root)
    {
        if (elements[root].parent != element_handle{})
        {
            continue;
        }

        order.clear();
        auto current = root;
        while (true)
        {
            if (visited[current])
            {
                throw std::invalid_argument("layout snapshot contains a cycle");
            }

            visited[current] = 1;
            local[current] = static_cast<std::uint32_t>(order.size());
            order.push_back(current);
            if (elements[current].first_child != element_handle{})
            {
                current = lookup(elements[current].first_child);
                continue;
            }

            std::size_t climbed = 0;
            while (current != root && elements[current].next_sibling == element_handle{})
            {
                current = lookup(elements[current].parent);
                if (current == no_layout_node || ++climbed > elements.size())
                {
                    throw std::invalid_argument("invalid layout snapshot ancestry");
                }
            }

            if (current == root)
            {
                break;
            }

            current = lookup(elements[current].next_sibling);
        }

        auto source = std::make_shared<layout_source>(memory);
        const auto link = [&](element_handle handle)
        {
            const auto index = lookup(handle);
            return index == no_layout_node ? no_layout_node : local[index];
        };
        append_nodes(*source, snapshot, order, link);
        order_stack_children(*source);

        total += order.size();
        result.roots.push_back(std::move(source));
    }

    if (total != elements.size())
    {
        throw std::invalid_argument("layout snapshot has no valid root forest");
    }

    return result;
}
} // namespace atlas::ui::detail
