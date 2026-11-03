/**
 * @file
 * @brief Stack distribution policies and sparse parent-owned child weights/order.
 */
#pragma once
#include <atlas/ui/commands.hpp>

namespace atlas::ui
{
/// @brief Allocate only nonnegative free space after border sizes, margins and minimum gaps.
enum class stack_distribution : std::uint8_t
{
    start,         ///< Pack against the leading content edge.
    center,        ///< Equal leading/trailing free space.
    end,           ///< Pack against the trailing content edge.
    space_between, ///< Extra space only between children; a single child stays at start.
    space_around,  ///< Equal interior space, half that amount at each edge.
    space_evenly   ///< Equal interior and edge free space.
};

/// @brief Common fixed child requests always retain their size.
enum class stack_fill : std::uint8_t
{
    none,    ///< Preserve measured border extents.
    weighted ///< Grow automatic axes using child weights, respecting maxima.
};

/// @brief Minimum gaps, margins and fixed/minimum extents can still overflow.
enum class stack_overflow : std::uint8_t
{
    allow, ///< Keep measured sizes and overflow the trailing edge.
    shrink ///< Shrink automatic axes using child weights, respecting minima.
};

/// @brief Shared by horizontal/vertical relationships; no paint depth is encoded here.
struct stack_slot
{
    std::int32_t order = 0; ///< Ascending key; ties retain sibling insertion order.
    float grow = 1;         ///< Finite nonnegative surplus weight; zero opts out.
    float shrink = 1;       ///< Finite nonnegative deficit weight; zero opts out.
    bool operator==(const stack_slot&) const = default; ///< Exact override/cache equality.
};

/// @brief Owned snapshot entry; defaults have no sparse record.
struct stack_slot_record
{
    element_handle target; ///< Exact captured child identity.
    stack_slot slot;       ///< Explicit nondefault parent-owned metadata.
};
} // namespace atlas::ui
