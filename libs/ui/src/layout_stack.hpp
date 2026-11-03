/** @file @brief Bounded stack scratch and main-axis sizing shared by both orientations. */
#pragma once
#include "layout_flow.hpp"
#include "layout_numeric.hpp"
#include "layout_state.hpp"

namespace atlas::ui::detail
{
/// @brief One capped-weight breakpoint; input weights stay in immutable sparse storage.
struct weighted_slot
{
    double cutoff;            ///< Size capacity divided by positive weight.
    double suffix_weight = 0; ///< Remaining active weight without subtractive cancellation.
    std::uint32_t index;      ///< Local node whose output cell is exclusive to this branch.
    std::uint32_t ordinal;    ///< Resolved sibling order, independent from breakpoint sorting.
};

/// @brief Reuse one vector across panels in the same branch, never one allocation per child/job.
struct stack_workspace
{
    explicit stack_workspace(std::pmr::memory_resource* memory) : weights(0, memory)
    {
    } ///< Borrow the retained branch resource until all numeric work returns.

    std::pmr::vector<weighted_slot> weights; ///< Populated only for active grow/shrink work.
};

/// @brief Axis adapter keeps arithmetic orientation-independent without copying the tree.
struct stack_axis
{
    const measured_branch& input; ///< Immutable desired extents and slot policies.
    arranged_branch& output;      ///< Exclusive mutable numeric cells.
    bool horizontal;              ///< Select width or height consistently.
    const std::stop_token& stop;  ///< Cooperative stop owned by the joined phase.

    float desired(std::uint32_t index) const;  ///< Original measured border size.
    float& current(std::uint32_t index) const; ///< Current arranged border size.
    float minimum(std::uint32_t index) const;  ///< Own lower border limit.
    float maximum(std::uint32_t index) const;  ///< Own upper border limit.
    bool fixed(std::uint32_t index) const;     ///< Common fixed requests never participate.
    double weight(std::uint32_t index,
                  bool growing) const;         ///< Positive float weights promoted before sums.
    double margins(std::uint32_t index) const; ///< Both outer margins promoted before addition.
};

struct stack_spacing
{
    double leading = 0; ///< Nonnegative outer free space.
    double gap = 0;     ///< Minimum fixed gap plus distributed free space.
}; ///< Separate packing from border-size allocation.

void redistribute_stack(
    const stack_axis& axis, double delta, bool growing,
    stack_workspace& scratch); ///< Capped weighted water filling, then bounded rounding correction.
void shrink_stack(const stack_axis& axis, double available,
                  stack_workspace& scratch); ///< Compute retained size from minima to avoid
                                             ///< catastrophic cancellation.
void correct_stack_rounding(
    const stack_axis& axis, std::span<const weighted_slot> weights, double residual,
    bool growing); ///< Stable bounded correction of float conversion residual.
stack_spacing
prepare_stack(const stack_axis& axis, std::uint32_t parent, double extent,
              stack_workspace& scratch); ///< Resolve allocation before assigning child slots.
} // namespace atlas::ui::detail
