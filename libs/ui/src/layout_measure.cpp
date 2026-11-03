/** @file @brief Stable bottom-up natural sizing without recursive stack growth. */
#include "layout_state.hpp"
#include <algorithm>

namespace atlas::ui::detail
{
/// @brief Parent sizing reads only children that already completed in reverse preorder.
measure_output measure_layout(measure_input source, const std::stop_token& stop)
{
    auto result = std::make_shared<measured_branch>(std::move(source));
    const auto& nodes = result->source->nodes;
    for (std::size_t position = nodes.size(); position != 0; --position)
    {
        if (stop.stop_requested())
        {
            throw jobs::job_cancelled{};
        }

        const auto index = position - 1;
        const auto& value = nodes[index];
        double width = 0;
        double height = 0;
        std::size_t children = 0;
        for (auto child = value.first; child != no_layout_node; child = nodes[child].next)
        {
            if (stop.stop_requested())
            {
                throw jobs::job_cancelled{};
            }

            const auto desired = result->desired[child];
            const auto margin = nodes[child].style.margin;
            const double outer_width =
                static_cast<double>(desired.width) + margin.left + margin.right;
            const double outer_height =
                static_cast<double>(desired.height) + margin.top + margin.bottom;
            width = value.style.kind == layout_kind::horizontal ? width + outer_width
                                                                : std::max(width, outer_width);
            height = value.style.kind == layout_kind::vertical ? height + outer_height
                                                               : std::max(height, outer_height);
            ++children;
        }

        if (children > 1)
        {
            const auto gaps = static_cast<double>(children - 1) * value.style.spacing;
            if (value.style.kind == layout_kind::horizontal)
            {
                width += gaps;
            }

            if (value.style.kind == layout_kind::vertical)
            {
                height += gaps;
            }
        }

        width = std::max(width, static_cast<double>(value.style.intrinsic.width)) +
                value.style.padding.left + value.style.padding.right;
        height = std::max(height, static_cast<double>(value.style.intrinsic.height)) +
                 value.style.padding.top + value.style.padding.bottom;
        const auto& limits = value.style.constraints;
        width = std::clamp(value.width_fixed ? value.width : width,
                           static_cast<double>(limits.minimum.width),
                           static_cast<double>(limits.maximum.width));
        height = std::clamp(value.height_fixed ? value.height : height,
                            static_cast<double>(limits.minimum.height),
                            static_cast<double>(limits.maximum.height));
        result->desired[index] = {layout_float(width), layout_float(height)};
    }

    return result;
}

/// @brief Independent roots produce independent owned arrays, published only after joining.
void measure_roots(const phase_range<measure_input, measure_output>& range)
{
    for (std::size_t index = 0; index < range.inputs.size(); ++index)
    {
        range.throw_if_cancelled();
        range.outputs[index] = measure_layout(range.inputs[index], range.cancellation);
    }
}
} // namespace atlas::ui::detail
