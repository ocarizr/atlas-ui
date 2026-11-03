/** @file @brief Lightweight axis reads borrow immutable arrays and exclusive output cells. */
#include "layout_stack.hpp"

namespace atlas::ui::detail
{
/// @brief Common desired size already includes the child's padding and own constraints.
float stack_axis::desired(std::uint32_t index) const
{
    return horizontal ? input.desired[index].width : input.desired[index].height;
}

/// @brief No other branch/job writes this child's arranged geometry.
float& stack_axis::current(std::uint32_t index) const
{
    auto& bounds = output.items[index].geometry.bounds;
    return horizontal ? bounds.width : bounds.height;
}

/// @brief Both axes use the same clamping contract.
float stack_axis::minimum(std::uint32_t index) const
{
    const auto value = input.source->nodes[index].style.constraints.minimum;
    return horizontal ? value.width : value.height;
}

/// @brief Positive infinity is a valid unbounded maximum.
float stack_axis::maximum(std::uint32_t index) const
{
    const auto value = input.source->nodes[index].style.constraints.maximum;
    return horizontal ? value.width : value.height;
}

/// @brief Explicit zero is fixed as well; intrinsic hints remain eligible.
bool stack_axis::fixed(std::uint32_t index) const
{
    const auto& node = input.source->nodes[index];
    return horizontal ? node.width_fixed : node.height_fixed;
}

/// @brief Zero weights opt out even when the parent enables allocation.
double stack_axis::weight(std::uint32_t index, bool growing) const
{
    const auto& slot = stack_placement(*input.source, input.source->nodes[index]);
    return growing ? slot.grow : slot.shrink;
}

/// @brief Promotion avoids overflowing an intermediate sum of two finite floats.
double stack_axis::margins(std::uint32_t index) const
{
    const auto value = input.source->nodes[index].style.margin;
    return horizontal ? static_cast<double>(value.left) + value.right
                      : static_cast<double>(value.top) + value.bottom;
}
} // namespace atlas::ui::detail
