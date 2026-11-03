/** @file @brief Stable shrink solves retained capacity instead of subtracting enormous losses. */
#include "layout_stack.hpp"
#include <algorithm>

namespace atlas::ui::detail
{
namespace
{
struct retention
{
    double level = 0;       ///< Active breakpoint; never subtract a tiny residual from this value.
    double extra = 0;       ///< Additional retained amount per unit active weight.
    std::size_t active = 0; ///< Prefix whose retained size exceeds its minimum.
}; ///< Pivoted water filling preserves small available extents for very large desired inputs.

/// @brief Activate descending capacity/weight groups until the remaining size fits one interval.
retention solve(std::span<const weighted_slot> weights, const stack_axis& axis, double remaining)
{
    if (weights.empty() || remaining == 0)
    {
        return {};
    }

    std::size_t active = 0;
    double total_weight = 0;
    double level = weights[0].cutoff;
    while (active < weights.size())
    {
        while (active < weights.size() && weights[active].cutoff == level)
        {
            total_weight += axis.weight(weights[active].index, false);
            ++active;
        }

        const double next = active < weights.size() ? weights[active].cutoff : 0;
        const double step = (level - next) * total_weight;
        if (remaining <= step || active == weights.size())
        {
            return {level, remaining / total_weight, active};
        }

        remaining -= step;
        level = next;
    }

    return {};
}
} // namespace

/// @brief Only scratch is reordered; output identities and resolved sibling links stay stable.
void shrink_stack(const stack_axis& axis, double available, stack_workspace& scratch)
{
    auto& weights = scratch.weights;
    std::sort(weights.begin(), weights.end(),
              [](const weighted_slot& left, const weighted_slot& right)
              {
                  return left.cutoff != right.cutoff ? left.cutoff > right.cutoff
                                                     : left.index < right.index;
              });
    const auto solution = solve(weights, axis, available);
    double residual = 0;
    for (std::size_t index = 0; index < weights.size(); ++index)
    {
        if (axis.stop.stop_requested())
        {
            throw jobs::job_cancelled{};
        }

        const auto& entry = weights[index];
        const double extra =
            index < solution.active
                ? axis.weight(entry.index, false) * (entry.cutoff - solution.level + solution.extra)
                : 0;
        const double ideal = std::clamp(axis.minimum(entry.index) + extra,
                                        static_cast<double>(axis.minimum(entry.index)),
                                        static_cast<double>(axis.desired(entry.index)));
        axis.current(entry.index) = layout_float(ideal);
        residual += ideal - axis.current(entry.index);
    }

    correct_stack_rounding(axis, weights, residual, false);
}
} // namespace atlas::ui::detail
