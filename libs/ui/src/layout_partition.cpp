/** @file @brief Refine coarse sibling forests at panel boundaries under a global range cap. */
#include "layout_partition.hpp"
#include <algorithm>

namespace atlas::ui::detail
{
namespace
{
/// @brief Cancellation remains cooperative during deep planning, before any workers borrow data.
void cancellation_point(const std::stop_token& stop)
{
    if (stop.stop_requested())
    {
        throw jobs::job_cancelled{};
    }
}

/// @brief Search every depth through unary wrappers; leaf-only panels remain indivisible work.
std::optional<std::pair<measure_interval, measure_interval>>
split(const layout_source& source, const measure_partition& plan, measure_interval range,
      std::size_t grain, const std::stop_token& stop)
{
    while (range.last - range.first >= grain && (range.last - range.first) / 2 >= grain)
    {
        cancellation_point(stop);
        std::uint32_t panel = no_layout_node;
        std::uint32_t cut = no_layout_node;
        auto distance = std::numeric_limits<std::uint32_t>::max();
        const auto middle = range.first + (range.last - range.first) / 2;
        for (auto index = range.first; index < range.last; index = plan.ends[index])
        {
            cancellation_point(stop);
            if (source.nodes[index].first == no_layout_node)
            {
                continue;
            }

            if (panel != no_layout_node && index - range.first >= grain &&
                range.last - index >= grain)
            {
                const auto delta = index > middle ? index - middle : middle - index;
                if (delta < distance)
                {
                    distance = delta;
                    cut = index;
                }
            }

            panel = index;
        }

        if (cut != no_layout_node)
        {
            return std::pair{measure_interval{range.first, cut}, measure_interval{cut, range.last}};
        }

        if (panel == no_layout_node)
        {
            break;
        }

        // Descend into this panel's children; unselected ancestors/siblings stay on the
        // coordinator.
        range = {panel + 1, plan.ends[panel]};
    }

    return std::nullopt;
}
} // namespace

/// @brief Subtree endpoints follow hierarchy indices, independently of sorted private sibling
/// links.
measure_partition partition_measure(const layout_source& source, const layout_limits& limits,
                                    const std::stop_token& stop)
{
    measure_partition result{source.memory.get()};
    result.ends.resize(source.nodes.size());
    for (std::uint32_t index = 0; index < source.nodes.size(); ++index)
    {
        cancellation_point(stop);
        result.ends[index] = index + 1;
    }

    for (std::size_t position = source.nodes.size(); position != 0; --position)
    {
        cancellation_point(stop);
        const auto parent = source.nodes[position - 1].parent;
        if (parent != no_layout_node)
        {
            result.ends[parent] = std::max(result.ends[parent], result.ends[position - 1]);
        }
    }

    result.ranges.reserve(limits.max_chunks);
    result.ranges.push_back({0, static_cast<std::uint32_t>(source.nodes.size())});
    while (result.ranges.size() < limits.max_chunks)
    {
        cancellation_point(stop);
        auto chosen = result.ranges.end();
        for (auto range = result.ranges.begin(); range != result.ranges.end(); ++range)
        {
            if (!range->terminal && (chosen == result.ranges.end() ||
                                     range->last - range->first > chosen->last - chosen->first))
            {
                chosen = range;
            }
        }

        if (chosen == result.ranges.end())
        {
            break;
        }

        const auto parts = split(source, result, *chosen, limits.subtree_grain, stop);
        if (!parts)
        {
            chosen->terminal = true;
            continue;
        }

        *chosen = parts->first;
        result.ranges.push_back(parts->second);
    }

    std::sort(result.ranges.begin(), result.ranges.end(),
              [](const auto& left, const auto& right)
              {
                  return left.first < right.first;
              });
    return result;
}
} // namespace atlas::ui::detail
