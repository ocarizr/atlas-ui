/**
 * @file
 * @brief Typed container policies, separate from common element sizing and child slots.
 */
#pragma once
#include <atlas/ui/stack_layout.hpp>
#include <cstdint>
#include <variant>

namespace atlas::ui
{
struct leaf_layout
{
    bool operator==(const leaf_layout&) const = default; ///< Intrinsic content without children.
}; ///< No child slots.

struct size_box_layout
{
    bool operator==(const size_box_layout&) const = default; ///< At most one child.
}; ///< Single content slot inside padding.

struct overlay_layout
{
    bool operator==(const overlay_layout&) const = default; ///< Shared content rectangle.
}; ///< Default insertion-ordered panel.

struct horizontal_layout
{
    float spacing = 0; ///< Finite nonnegative gap between children.
    stack_distribution distribution = stack_distribution::start; ///< Remaining-space placement.
    stack_fill fill = stack_fill::none;                          ///< Explicit growth policy.
    stack_overflow overflow = stack_overflow::allow;             ///< Explicit deficit policy.
    bool operator==(const horizontal_layout&) const = default;   ///< Exact policy equality.
}; ///< Natural-width child slots.

struct vertical_layout
{
    float spacing = 0; ///< Finite nonnegative gap between children.
    stack_distribution distribution = stack_distribution::start; ///< Remaining-space placement.
    stack_fill fill = stack_fill::none;                          ///< Explicit growth policy.
    stack_overflow overflow = stack_overflow::allow;             ///< Explicit deficit policy.
    bool operator==(const vertical_layout&) const = default;     ///< Exact policy equality.
}; ///< Natural-height child slots.

/// @brief Explicitly avoid parent-relative/content-size equations.
enum class canvas_measure : std::uint8_t
{
    intrinsic,        ///< Children do not contribute to desired canvas size.
    absolute_children ///< Include only zero-anchored desired-size children; reject other slots.
};

struct canvas_layout
{
    canvas_measure measure = canvas_measure::intrinsic;    ///< Noncircular default.
    bool operator==(const canvas_layout&) const = default; ///< Exact policy equality.
}; ///< Free placement with canvas-specific child slots.

using panel_layout = std::variant<overlay_layout, leaf_layout, size_box_layout, horizontal_layout,
                                  vertical_layout, canvas_layout>; ///< Closed typed policies.
} // namespace atlas::ui
