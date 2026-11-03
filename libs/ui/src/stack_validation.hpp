/** @file @brief Typed stack policy and parent-relationship validation. */
#pragma once
#include <atlas/ui/panel_layout.hpp>

namespace atlas::ui::detail
{
bool stack_panel(
    const panel_layout& panel) noexcept; ///< Both axis variants share one slot contract.
void validate_stack_policy(stack_distribution distribution, stack_fill fill,
                           stack_overflow overflow); ///< Reject unknown policy enums.
void validate_stack_slot(const stack_slot& slot);    ///< Weights are finite and nonnegative.
} // namespace atlas::ui::detail
