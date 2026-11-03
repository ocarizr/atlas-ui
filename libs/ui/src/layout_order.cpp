/** @file @brief Stable stack child ordering changes private numeric links, never the live tree. */
#include "layout_flow.hpp"
#include "layout_state.hpp"
#include <algorithm>
#include <tuple>

namespace atlas::ui::detail
{
namespace
{
/// @brief Insertion ordinal is independent of recycled public slot indices.
struct ordered_child
{
    std::uint32_t index;   ///< Local preorder identity.
    std::uint32_t ordinal; ///< Original sibling position.
    std::int32_t order;    ///< Authored/runtime ascending key.
};
} // namespace

/// @brief Scratch is allocated only when a nondefault order actually requires sorting.
void order_stack_children(layout_source& source)
{
    std::pmr::vector<ordered_child> scratch(0, source.memory.get());
    for (auto& parent : source.nodes)
    {
        const auto flow = describe_flow(parent.panel);
        if (!flow.horizontal && !flow.vertical)
        {
            continue;
        }

        bool custom = false;
        for (auto child = parent.first; child != no_layout_node; child = source.nodes[child].next)
        {
            custom = custom || stack_placement(source, source.nodes[child]).order != 0;
        }

        if (!custom)
        {
            continue;
        }

        scratch.clear();
        for (auto child = parent.first; child != no_layout_node; child = source.nodes[child].next)
        {
            scratch.push_back({child, static_cast<std::uint32_t>(scratch.size()),
                               stack_placement(source, source.nodes[child]).order});
        }

        std::sort(scratch.begin(), scratch.end(),
                  [](const ordered_child& left, const ordered_child& right)
                  {
                      return std::tie(left.order, left.ordinal) <
                             std::tie(right.order, right.ordinal);
                  });
        parent.first = scratch.front().index;
        for (std::size_t index = 0; index < scratch.size(); ++index)
        {
            source.nodes[scratch[index].index].next =
                index + 1 < scratch.size() ? scratch[index + 1].index : no_layout_node;
        }
    }
}
} // namespace atlas::ui::detail
