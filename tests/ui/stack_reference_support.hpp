/** @file @brief Small quadratic reference model intentionally differs from production water
 * filling. */
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace atlas::ui::tests
{
struct stack_sample
{
    float size;         ///< Natural border size.
    float minimum;      ///< Border lower limit.
    float maximum;      ///< Border upper limit.
    float grow;         ///< Absolute surplus weight.
    float shrink;       ///< Absolute deficit weight.
    float before;       ///< Leading margin.
    float after;        ///< Trailing margin.
    std::int32_t order; ///< Stable sort key.
    bool fixed;         ///< Never resized by the parent.
}; ///< Reproducible data for a small independent solver.

struct stack_reference
{
    std::vector<stack_sample> children; ///< Small owned per-scenario model.
    float extent;                       ///< Available content extent.
    float gap;                          ///< Minimum inter-child spacing.
}; ///< Both fill and shrink are enabled in reference comparisons.

/// @brief Repeatedly freeze capped children, then divide remaining space among the survivors.
inline std::vector<double> reference_sizes(const stack_reference& input)
{
    std::vector<double> sizes;
    std::vector<unsigned char> active;
    sizes.reserve(input.children.size());
    active.reserve(input.children.size());
    double occupied =
        input.children.empty() ? 0 : static_cast<double>(input.children.size() - 1) * input.gap;
    for (const auto& child : input.children)
    {
        sizes.push_back(child.size);
        occupied += static_cast<double>(child.size) + child.before + child.after;
    }

    const bool growing = occupied < input.extent;
    double remaining = std::abs(input.extent - occupied);
    for (const auto& child : input.children)
    {
        active.push_back(
            static_cast<unsigned char>(!child.fixed && (growing ? child.grow : child.shrink) > 0));
    }

    for (std::size_t pass = 0; pass <= input.children.size() && remaining > 1e-10; ++pass)
    {
        double weight = 0;
        for (std::size_t index = 0; index < active.size(); ++index)
        {
            if (active[index])
            {
                weight += growing ? input.children[index].grow : input.children[index].shrink;
            }
        }

        if (weight == 0)
        {
            break;
        }

        std::vector<std::size_t> capped;
        for (std::size_t index = 0; index < active.size(); ++index)
        {
            if (!active[index])
            {
                continue;
            }

            const auto& child = input.children[index];
            const double share = remaining * (growing ? child.grow : child.shrink) / weight;
            const double room =
                growing ? child.maximum - sizes[index] : sizes[index] - child.minimum;
            if (share >= room)
            {
                capped.push_back(index);
            }
        }

        if (capped.empty())
        {
            for (std::size_t index = 0; index < active.size(); ++index)
            {
                if (!active[index])
                {
                    continue;
                }

                const auto& child = input.children[index];
                const double share = remaining * (growing ? child.grow : child.shrink) / weight;
                sizes[index] += growing ? share : -share;
            }

            break;
        }

        for (const auto index : capped)
        {
            const auto& child = input.children[index];
            remaining -= growing ? child.maximum - sizes[index] : sizes[index] - child.minimum;
            sizes[index] = growing ? child.maximum : child.minimum;
            active[index] = 0;
        }
    }

    return sizes;
}
} // namespace atlas::ui::tests
