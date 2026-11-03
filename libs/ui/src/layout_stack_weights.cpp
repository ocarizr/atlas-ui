/** @file @brief Deterministic capped weighted allocation with O(n log n) bounded scratch work. */
#include "layout_stack.hpp"
#include <algorithm>
#include <cmath>

namespace atlas::ui::detail
{
namespace
{
/// @brief Compute the common multiplier without repeatedly rescanning saturated children.
double multiplier(std::span<const weighted_slot> weights, double remaining)
{
    double value = 0;
    for (const auto& entry : weights)
    {
        const double step = (entry.cutoff - value) * entry.suffix_weight;
        if (std::isinf(entry.cutoff) || remaining <= step)
        {
            return value + remaining / entry.suffix_weight;
        }

        remaining -= step;
        value = entry.cutoff;
    }

    return value;
}

} // namespace

/// @brief Offer rounding residual to the last participating child with room for that correction.
void correct_stack_rounding(const stack_axis& axis, std::span<const weighted_slot> weights,
                            double residual, bool growing)
{
    if (residual == 0)
    {
        return;
    }

    std::uint32_t candidate = no_layout_node;
    std::uint32_t ordinal = 0;
    for (const auto& entry : weights)
    {
        const double corrected = axis.current(entry.index) + residual;
        if (corrected >= axis.minimum(entry.index) && corrected <= axis.maximum(entry.index) &&
            (!growing || corrected >= axis.desired(entry.index)) &&
            (growing || corrected <= axis.desired(entry.index)))
        {
            // Pick the last node in resolved sibling order, not breakpoint order.
            if (candidate == no_layout_node || entry.ordinal > ordinal)
            {
                candidate = entry.index;
                ordinal = entry.ordinal;
            }
        }
    }

    if (candidate != no_layout_node)
    {
        axis.current(candidate) = layout_float(axis.current(candidate) + residual);
    }
}

/// @brief Caller supplies a positive finite amount; immutable inputs bound all individual changes.
void redistribute_stack(const stack_axis& axis, double delta, bool growing,
                        stack_workspace& scratch)
{
    auto& weights = scratch.weights;
    std::sort(weights.begin(), weights.end(),
              [](const weighted_slot& left, const weighted_slot& right)
              {
                  return left.cutoff != right.cutoff ? left.cutoff < right.cutoff
                                                     : left.index < right.index;
              });
    double suffix = 0;
    for (auto index = weights.size(); index != 0; --index)
    {
        auto& entry = weights[index - 1];
        suffix += axis.weight(entry.index, growing);
        entry.suffix_weight = suffix;
    }

    const auto factor = multiplier(weights, delta);
    double residual = 0;
    for (const auto& entry : weights)
    {
        if (axis.stop.stop_requested())
        {
            throw jobs::job_cancelled{};
        }

        const double original = axis.desired(entry.index);
        const double amount = axis.weight(entry.index, growing) * factor;
        const double ideal = std::clamp(original + (growing ? amount : -amount),
                                        static_cast<double>(axis.minimum(entry.index)),
                                        static_cast<double>(axis.maximum(entry.index)));
        axis.current(entry.index) = layout_float(ideal);
        residual += ideal - axis.current(entry.index);
    }

    correct_stack_rounding(axis, weights, residual, growing);
}
} // namespace atlas::ui::detail
