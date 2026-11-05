/** @file @brief Iterative isolated stacking contexts and inherited rectangular masks. */
#include "paint_state.hpp"
#include <algorithm>
#include <cmath>

namespace atlas::ui
{
namespace
{
/// @brief Reject coordinate overflow before publishing numeric output.
float checked(double value)
{
    if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
    {
        throw std::overflow_error("UI paint coordinate overflow");
    }

    return static_cast<float>(value);
}

/// @brief Validate public clip inputs, including their representable trailing edges.
void validate(layout_rect value)
{
    if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.width) ||
        !std::isfinite(value.height) || value.width < 0 || value.height < 0)
    {
        throw std::invalid_argument("invalid UI clip rectangle");
    }

    (void)checked(static_cast<double>(value.x) + value.width);
    (void)checked(static_cast<double>(value.y) + value.height);
}
} // namespace

/// @brief Empty intersection retains a finite clamped leading position and zero extent.
layout_rect intersect_clip(layout_rect a, layout_rect b)
{
    validate(a);
    validate(b);
    const auto x = std::max(a.x, b.x);
    const auto y = std::max(a.y, b.y);
    const auto right =
        std::min(static_cast<double>(a.x) + a.width, static_cast<double>(b.x) + b.width);
    const auto bottom =
        std::min(static_cast<double>(a.y) + a.height, static_cast<double>(b.y) + b.height);
    return {x, y, checked(std::max(0.0, right - x)), checked(std::max(0.0, bottom - y))};
}

namespace detail
{
/// @brief Resolve inherited state iteratively after stable stacking links have been prepared.
void prepare_paint(const element_snapshot& input, const layout_snapshot& geometry,
                   std::pmr::memory_resource* memory, const std::stop_token& stop,
                   paint_preparation& result, std::pmr::vector<presentation_region>* regions,
                   bool emit_quads)
{
    const auto slots = validate_paint_geometry(input, geometry);
    const auto nodes = input.elements();
    const auto layouts = geometry.items();
    paint_links links{nodes.size(), memory};
    order_paint(nodes, slots, memory, links);
    const auto& first = links.first;
    const auto& next = links.next;
    const auto none = nodes.size();

    struct visit
    {
        std::size_t index;   ///< Node in captured slot order.
        layout_point offset; ///< Inherited translation.
        layout_rect clip;    ///< Inherited effective rectangular viewport.
        float alpha;         ///< Ancestor opacity product.
        bool visible;        ///< Ancestor visibility conjunction.
    }; ///< Bounded explicit traversal stack, not recursive C++ execution.

    std::pmr::vector<visit> stack(0, memory);
    stack.reserve(nodes.size());
    if (emit_quads)
    {
        result.inputs.reserve(nodes.size());
        result.roots.reserve(nodes.size());
    }

    if (regions)
    {
        regions->reserve(nodes.size());
    }

    for (auto root = first[none]; root != none; root = next[root])
    {
        const auto before = result.inputs.size();
        stack.push_back({root, {}, input.viewport(), 1, true});
        while (!stack.empty())
        {
            check_paint_cancel(stop);
            auto current = stack.back();
            stack.pop_back();
            const auto& node = nodes[current.index];
            auto bounds = layouts[current.index].geometry.bounds;
            current.offset.x =
                checked(static_cast<double>(current.offset.x) + node.paint.translation.x);
            current.offset.y =
                checked(static_cast<double>(current.offset.y) + node.paint.translation.y);
            bounds.x = checked(static_cast<double>(bounds.x) + current.offset.x);
            bounds.y = checked(static_cast<double>(bounds.y) + current.offset.y);
            validate(bounds);
            current.alpha *= node.requested.opacity;
            current.visible = current.visible && node.requested.visible;
            if (node.paint.clip)
            {
                current.clip = intersect_clip(current.clip, bounds);
            }

            const auto covered = intersect_clip(current.clip, bounds);
            if (regions)
            {
                regions->push_back(
                    {node.target, node.parent, bounds, current.clip, current.visible});
            }

            if (emit_quads && node.paint.fill && current.visible && current.alpha > 0 &&
                node.paint.color[3] > 0 && covered.width > 0 && covered.height > 0)
            {
                auto style = node.paint;
                style.color[3] *= current.alpha;
                if (style.image)
                {
                    const auto fitted = fit_image(bounds, *style.image, style.uv);
                    bounds = fitted.bounds;
                    style.uv = fitted.uv;
                }

                if (bounds.width > 0 && bounds.height > 0)
                {
                    result.inputs.push_back({node.target, bounds, current.clip, style});
                }
            }

            const auto begin = stack.size();
            for (auto child = first[current.index]; child != none; child = next[child])
            {
                stack.push_back(
                    {child, current.offset, current.clip, current.alpha, current.visible});
            }

            std::reverse(stack.begin() + static_cast<std::ptrdiff_t>(begin), stack.end());
        }

        if (result.inputs.size() != before)
        {
            result.roots.push_back(result.inputs.size());
        }
    }
}
} // namespace detail
} // namespace atlas::ui
