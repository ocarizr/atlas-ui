/** @file @brief Stable bottom-up natural sizing without recursive stack growth. */
#include "layout_flow.hpp"
#include "layout_numeric.hpp"
#include "layout_state.hpp"
#include <algorithm>

namespace atlas::ui::detail
{
namespace
{
/// @brief Container aggregation is independent from common requested-size/constraint clamping.
std::pair<double, double> content_size(const measured_branch& result, std::size_t index,
                                       const std::stop_token& stop)
{
    const auto& nodes = result.source->nodes;
    const auto& value = nodes[index];
    const auto flow = describe_flow(value.panel);
    const auto* canvas = std::get_if<canvas_layout>(&value.panel);
    double width = 0;
    double height = 0;
    std::size_t children = 0;
    for (auto child = value.first; child != no_layout_node; child = nodes[child].next)
    {
        if (stop.stop_requested())
        {
            throw jobs::job_cancelled{};
        }

        const auto desired = result.desired[child];
        if (canvas)
        {
            if (canvas->measure == canvas_measure::absolute_children)
            {
                const auto extent = measure_canvas_child(nodes[child], desired,
                                                         placement(*result.source, nodes[child]));
                width = std::max(width, static_cast<double>(extent.width));
                height = std::max(height, static_cast<double>(extent.height));
            }

            continue;
        }

        const auto margin = nodes[child].style.margin;
        const double outer_width = static_cast<double>(desired.width) + margin.left + margin.right;
        const double outer_height =
            static_cast<double>(desired.height) + margin.top + margin.bottom;
        width = flow.horizontal ? width + outer_width : std::max(width, outer_width);
        height = flow.vertical ? height + outer_height : std::max(height, outer_height);
        ++children;
    }

    if (children > 1)
    {
        const auto gaps = static_cast<double>(children - 1) * flow.spacing;
        if (flow.horizontal)
        {
            width += gaps;
        }

        if (flow.vertical)
        {
            height += gaps;
        }
    }

    return {width, height};
}
} // namespace

/// @brief The same nonrecursive kernel serves serial ranges and joined ancestor reduction.
void measure_node(measured_branch& result, std::size_t index, const std::stop_token& stop)
{
    if (stop.stop_requested())
    {
        throw jobs::job_cancelled{};
    }

    const auto& value = result.source->nodes[index];
    auto [width, height] = content_size(result, index, stop);
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
    result.desired[index] = {layout_float(width), layout_float(height)};
}

/// @brief Parent sizing reads only children that already completed in reverse preorder.
void measure_nodes(measured_branch& result, std::span<layout_extent> output,
                   const std::stop_token& stop)
{
    const auto first = static_cast<std::size_t>(output.data() - result.desired.data());
    for (std::size_t position = first + output.size(); position != first; --position)
    {
        measure_node(result, position - 1, stop);
    }
}

/// @brief Serial and worker interval loops share a compilation unit for identical inlining chances.
measure_output measure_layout(measure_input source, const std::stop_token& stop)
{
    auto result = std::make_shared<measured_branch>(std::move(source));
    measure_nodes(*result, result->desired, stop);
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
