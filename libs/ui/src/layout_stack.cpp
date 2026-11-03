/** @file @brief Main-axis packing uses remaining space after bounded grow/shrink and fixed gaps. */
#include "layout_stack.hpp"
#include <algorithm>
#include <cmath>

namespace atlas::ui::detail
{
namespace
{
struct stack_space
{
    std::uint32_t parent; ///< Local container index.
    double extent;        ///< Arranged content extent.
    float gap;            ///< Minimum fixed spacing.
}; ///< Bundle convertible numeric parameters by their roles.

/// @brief Read current outer extents in resolved sibling order with double accumulation.
double occupied(const stack_axis& axis, const stack_space& space)
{
    double total = 0;
    std::size_t count = 0;
    const auto& nodes = axis.input.source->nodes;
    for (auto child = nodes[space.parent].first; child != no_layout_node; child = nodes[child].next)
    {
        if (axis.stop.stop_requested())
        {
            throw jobs::job_cancelled{};
        }

        total += axis.current(child) + axis.margins(child);
        ++count;
    }

    return total + (count > 1 ? static_cast<double>(count - 1) * space.gap : 0);
}

/// @brief Only nonfixed children with positive weight and remaining capacity allocate scratch.
void prepare_weights(const stack_axis& axis, std::uint32_t parent, bool growing,
                     stack_workspace& scratch)
{
    scratch.weights.clear();
    const auto& nodes = axis.input.source->nodes;
    std::uint32_t ordinal = 0;
    for (auto child = nodes[parent].first; child != no_layout_node; child = nodes[child].next)
    {
        if (axis.stop.stop_requested())
        {
            throw jobs::job_cancelled{};
        }

        const auto weight = axis.weight(child, growing);
        const double capacity =
            growing ? static_cast<double>(axis.maximum(child)) - axis.desired(child)
                    : static_cast<double>(axis.desired(child)) - axis.minimum(child);
        if (!axis.fixed(child) && weight > 0 && capacity > 0)
        {
            scratch.weights.push_back({capacity / weight, 0, child, ordinal});
        }

        ++ordinal;
    }
}

/// @brief Subtract only mandatory sizes, never a huge desired total minus a nearly equal deficit.
double shrink_budget(const stack_axis& axis, const stack_space& space)
{
    const auto& nodes = axis.input.source->nodes;
    double mandatory = 0;
    std::size_t count = 0;
    for (auto child = nodes[space.parent].first; child != no_layout_node; child = nodes[child].next)
    {
        if (axis.stop.stop_requested())
        {
            throw jobs::job_cancelled{};
        }

        const bool participant = !axis.fixed(child) && axis.weight(child, false) > 0;
        mandatory +=
            (participant ? axis.minimum(child) : axis.desired(child)) + axis.margins(child);
        ++count;
    }

    mandatory += count > 1 ? static_cast<double>(count - 1) * space.gap : 0;
    return std::max(0., space.extent - mandatory);
}

/// @brief Empty/singleton panels use explicit nondividing edge rules.
stack_spacing pack(stack_distribution distribution, double free, std::size_t count, float gap)
{
    if (count == 0)
    {
        return {};
    }

    switch (distribution)
    {
        case stack_distribution::center:
            return {free / 2, gap};
        case stack_distribution::end:
            return {free, gap};
        case stack_distribution::space_between:
            return {0, gap + (count > 1 ? free / static_cast<double>(count - 1) : 0)};
        case stack_distribution::space_around:
            return {free / (2 * static_cast<double>(count)),
                    gap + free / static_cast<double>(count)};
        case stack_distribution::space_evenly:
            return {free / static_cast<double>(count + 1),
                    gap + free / static_cast<double>(count + 1)};
        case stack_distribution::start:
            return {0, gap};
    }

    throw std::logic_error("validated stack distribution became invalid");
}
} // namespace

/// @brief Default natural/start/overflow policy retains the previous single-pass placement path.
stack_spacing prepare_stack(const stack_axis& axis, std::uint32_t parent, double extent,
                            stack_workspace& scratch)
{
    const auto flow = describe_flow(axis.input.source->nodes[parent].panel);
    if ((!flow.horizontal && !flow.vertical) ||
        (flow.fill == stack_fill::none && flow.overflow == stack_overflow::allow &&
         flow.distribution == stack_distribution::start))
    {
        return {0, flow.spacing};
    }

    const stack_space space{parent, extent, flow.spacing};
    const double delta = extent - occupied(axis, space);
    if ((delta > 0 && flow.fill == stack_fill::weighted) ||
        (delta < 0 && flow.overflow == stack_overflow::shrink))
    {
        prepare_weights(axis, parent, delta > 0, scratch);
        if (delta > 0)
        {
            redistribute_stack(axis, delta, true, scratch);
        }
        else
        {
            shrink_stack(axis, shrink_budget(axis, space), scratch);
        }
    }

    std::size_t count = 0;
    const auto& nodes = axis.input.source->nodes;
    for (auto child = nodes[parent].first; child != no_layout_node; child = nodes[child].next)
    {
        ++count;
    }

    const auto free = std::max(0., extent - occupied(axis, space));
    return pack(flow.distribution, free, count, flow.spacing);
}
} // namespace atlas::ui::detail
