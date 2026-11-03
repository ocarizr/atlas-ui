/** @file @brief Structural edits preserve relationship scope and prevent dangling dependencies. */
#include "tree_state.hpp"

namespace atlas::ui
{
/// @brief A referenced source cannot move or disappear independently of its sibling dependents.
void element_tree::impl::require_unreferenced(std::uint32_t index) const
{
    if (registry.view<const layout_boundaries>().size() == 0)
    {
        return;
    }

    const auto parent = at(index).parent;
    if (parent == none)
    {
        return;
    }

    const auto source = handle(index);
    for (auto child = at(parent).first; child != none; child = at(child).next)
    {
        const auto value = child_boundaries(child);
        if ((value.horizontal && value.horizontal->source == source) ||
            (value.vertical && value.vertical->source == source))
        {
            throw std::invalid_argument(
                "reset dependent UI boundaries before moving or removing their source");
        }
    }
}

/// @brief Reject conversions that would introduce content-size equations or discard dependencies.
void element_tree::impl::validate_boundary_panel(std::uint32_t index,
                                                 const panel_layout& panel) const
{
    if (registry.view<const layout_boundaries>().size() == 0)
    {
        return;
    }

    const auto* canvas = std::get_if<canvas_layout>(&panel);
    if (canvas && canvas->measure == canvas_measure::intrinsic)
    {
        return;
    }

    for (auto child = at(index).first; child != none; child = at(child).next)
    {
        if (child_boundaries(child) != layout_boundaries{})
        {
            throw std::invalid_argument("reset UI boundaries before changing their canvas domain");
        }
    }
}

/// @brief No scratch allocation or modification: removal retains its collected descendant list.
void element_tree::impl::invalidate_boundary_domain(std::uint32_t index)
{
    const auto& components = registry;
    if (components.view<const layout_boundaries>().size() == 0)
    {
        return;
    }

    if (!std::holds_alternative<canvas_layout>(at(index).panel))
    {
        return;
    }

    bool related = false;
    for (auto child = at(index).first; child != none; child = at(child).next)
    {
        related = related || child_boundaries(child) != layout_boundaries{};
    }

    if (!related)
    {
        return;
    }

    auto current = index;
    while (true)
    {
        auto& value = at(current);
        value.dirty = {true, true};
        value.computed_valid = false;
        if (value.first != none)
        {
            current = value.first;
            continue;
        }

        while (current != index && at(current).next == none)
        {
            current = at(current).parent;
        }

        if (current == index)
        {
            break;
        }

        current = at(current).next;
    }
}
} // namespace atlas::ui
