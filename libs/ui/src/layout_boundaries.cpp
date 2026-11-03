/** @file @brief Directional outer-edge placement with explicit remaining-space and failure policy.
 */
#include "layout_numeric.hpp"
#include "layout_state.hpp"
#include <algorithm>

namespace atlas::ui::detail
{
namespace
{
/// @brief Named axis roles prevent positional size/coordinate ambiguity in the numeric kernel.
struct boundary_axis_input
{
    double origin;            ///< Parent content start.
    double extent;            ///< Parent content extent.
    double source_start;      ///< Source outer leading edge.
    double source_end;        ///< Source outer trailing edge.
    float before;             ///< Dependent leading margin.
    float after;              ///< Dependent trailing margin.
    float desired;            ///< Natural dependent border extent.
    float minimum;            ///< Hard dependent minimum.
    float maximum;            ///< Hard dependent maximum.
    bool fixed;               ///< Explicit request wins over remaining-space allocation.
    boundary_relation policy; ///< Validated direction/gap/sizing/overflow.
};

/// @brief All arithmetic remains double until checked finite rectangle publication.
std::pair<float, float> resolve_axis(const boundary_axis_input& input)
{
    const bool after = input.policy.side == boundary_side::after;
    const double edge =
        after ? input.source_end + input.policy.gap : input.source_start - input.policy.gap;
    const double available =
        std::max(0., (after ? input.origin + input.extent - edge : edge - input.origin) -
                         input.before - input.after);
    const double natural = input.policy.sizing == boundary_sizing::remaining && !input.fixed
                               ? available
                               : input.desired;
    const double size =
        std::clamp(natural, static_cast<double>(input.minimum), static_cast<double>(input.maximum));
    const double position = after ? edge + input.before : edge - input.after - size;
    if (input.policy.overflow == boundary_overflow::reject &&
        (position - input.before < input.origin ||
         position + size + input.after > input.origin + input.extent))
    {
        throw std::invalid_argument("UI boundary outer bounds exceed parent content");
    }

    (void)layout_float(position + size);
    return {layout_float(position), layout_float(size)};
}
} // namespace

/// @brief Dependency order guarantees source borders are complete before this child is placed.
void place_boundaries(arranged_branch& result, std::uint32_t child, slot_rect content)
{
    const auto& source = *result.measured->source;
    const auto& node = source.nodes[child];
    const auto& style = node.style;
    auto& bounds = result.items[child].geometry.bounds;
    const auto begin = std::lower_bound(source.boundaries.begin(), source.boundaries.end(), child,
                                        [](const resolved_boundary& edge, std::uint32_t target)
                                        {
                                            return edge.target < target;
                                        });
    canvas_axes axes;
    for (auto entry = begin; entry != source.boundaries.end() && entry->target == child; ++entry)
    {
        (entry->horizontal ? axes.horizontal : axes.vertical) = false;
    }

    bounds =
        place_canvas(node, result.measured->desired[child], content, placement(source, node), axes);
    for (auto entry = begin; entry != source.boundaries.end() && entry->target == child; ++entry)
    {
        const auto& dependency = source.nodes[entry->source];
        const auto rect = result.items[entry->source].geometry.bounds;
        const auto margin = dependency.style.margin;
        const auto desired = result.measured->desired[child];
        if (entry->horizontal)
        {
            const auto [position, size] = resolve_axis(
                {content.x, content.width, static_cast<double>(rect.x) - margin.left,
                 static_cast<double>(rect.x) + rect.width + margin.right, style.margin.left,
                 style.margin.right, desired.width, style.constraints.minimum.width,
                 style.constraints.maximum.width, node.width_fixed, entry->policy});
            bounds.x = position;
            bounds.width = size;
        }
        else
        {
            const auto [position, size] = resolve_axis(
                {content.y, content.height, static_cast<double>(rect.y) - margin.top,
                 static_cast<double>(rect.y) + rect.height + margin.bottom, style.margin.top,
                 style.margin.bottom, desired.height, style.constraints.minimum.height,
                 style.constraints.maximum.height, node.height_fixed, entry->policy});
            bounds.y = position;
            bounds.height = size;
        }
    }
}
} // namespace atlas::ui::detail
