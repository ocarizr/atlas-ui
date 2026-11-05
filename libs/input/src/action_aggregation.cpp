/** @file @brief Priority consumption, typed composites and deterministic modifier ordering. */
#include "input_state.hpp"
#include <cmath>

namespace atlas::input
{
namespace
{
/// @brief Aggregation cannot silently overflow the public float value format.
float checked(double value)
{
    if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
    {
        throw std::overflow_error("input action aggregate overflow");
    }

    return static_cast<float>(value);
}
} // namespace

/// @brief Matching controls are contiguous; work is bounded by mappings and device capacity.
void input_system::impl::recompute(bool cancelled)
{
    for (auto& a : actions)
    {
        a.sum = {};
        a.winner = 0;
        a.selected_epoch = 0;
    }

    for (auto& c : controls)
    {
        c.claim = 0;
    }

    for (const auto index : order)
    {
        const auto& context = contexts[index];
        if (!context.enabled)
        {
            continue;
        }

        for (const auto& b : std::span{bindings}.subspan(context.begin, context.count))
        {
            auto& a = actions[b.action];
            if (a.winner && a.winner != context.id)
            {
                continue;
            }

            a.winner = context.id;
            a.selected_epoch = context.activated;
            const auto family = std::pair{b.definition.control.device, b.definition.control.code};
            auto control =
                std::lower_bound(controls.begin(), controls.end(), family,
                                 [](const auto& state, auto key)
                                 {
                                     return std::pair{state.device.kind, state.code} < key;
                                 });
            for (; control != controls.end() &&
                   std::pair{control->device.kind, control->code} == family;
                 ++control)
            {
                if ((b.definition.source && b.definition.source != control->source) ||
                    (b.definition.device && b.definition.device != control->device.id) ||
                    (control->claim && control->claim != context.id))
                {
                    continue;
                }

                if (b.definition.consume)
                {
                    control->claim = context.id;
                }

                if (context.activation == activation_policy::require_neutral &&
                    control->neutral < context.activated)
                {
                    continue;
                }

                auto value = b.definition.modifier ? b.definition.modifier->apply(control->value)
                                                   : control->value;
                validate_value(value);
                if (a.type == value_type::button)
                {
                    a.sum.x = std::max(a.sum.x, std::max(std::abs(value.x), std::abs(value.y)));
                }
                else
                {
                    a.sum.x = checked(static_cast<double>(a.sum.x) + value.x);
                    if (a.type == value_type::axis2d)
                    {
                        a.sum.y = checked(static_cast<double>(a.sum.y) + value.y);
                    }
                }
            }
        }
    }

    for (auto& a : actions)
    {
        if (a.winner && a.modifier)
        {
            a.sum = a.modifier->apply(a.sum);
        }

        validate_value(a.sum);
        if (a.type == value_type::button)
        {
            a.sum = {std::max(std::abs(a.sum.x), std::abs(a.sum.y)) >= 0.5F ? 1.F : 0.F, 0};
        }

        if (a.type == value_type::axis1d)
        {
            a.sum.y = 0;
        }

        transition(a, cancelled);
    }
}
} // namespace atlas::input
