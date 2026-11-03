/** @file @brief Stack weights and enum guards run before any live state mutation. */
#include "stack_validation.hpp"
#include <cmath>
#include <stdexcept>

namespace atlas::ui::detail
{
/// @brief Canvas/overlay/leaf relationships cannot accidentally acquire stack-only parameters.
bool stack_panel(const panel_layout& panel) noexcept
{
    return std::holds_alternative<horizontal_layout>(panel) ||
           std::holds_alternative<vertical_layout>(panel);
}

/// @brief The closed typed policy set never silently maps malformed data to a default.
void validate_stack_policy(stack_distribution distribution, stack_fill fill,
                           stack_overflow overflow)
{
    if ((distribution != stack_distribution::start && distribution != stack_distribution::center &&
         distribution != stack_distribution::end &&
         distribution != stack_distribution::space_between &&
         distribution != stack_distribution::space_around &&
         distribution != stack_distribution::space_evenly) ||
        (fill != stack_fill::none && fill != stack_fill::weighted) ||
        (overflow != stack_overflow::allow && overflow != stack_overflow::shrink))
    {
        throw std::invalid_argument("invalid stack distribution, fill or overflow policy");
    }
}

/// @brief Integer keys are unrestricted; zero weights are explicit participation opt-outs.
void validate_stack_slot(const stack_slot& slot)
{
    if (!std::isfinite(slot.grow) || slot.grow < 0 || !std::isfinite(slot.shrink) ||
        slot.shrink < 0)
    {
        throw std::invalid_argument("stack weights must be finite and nonnegative");
    }
}
} // namespace atlas::ui::detail
