/**
 * @file
 * @brief Bounded directional sibling relationships in an intrinsic canvas domain.
 */
#pragma once
#include <atlas/ui/layout_slot.hpp>
#include <optional>

namespace atlas::ui
{
/// @brief Direction is explicit; no mutual constraint equation is inferred.
enum class boundary_side : std::uint8_t
{
    after, ///< Target outer start follows the source outer end plus gap.
    before ///< Target outer end precedes the source outer start minus gap.
};

/// @brief Allocation is independent from the direction of the sibling dependency.
enum class boundary_sizing : std::uint8_t
{
    desired,  ///< Retain the measured size, including explicit fixed requests.
    remaining ///< Fill toward the parent content edge, subject to own fixed/min/max constraints.
};

/// @brief An impossible parent interval is either explicit overflow or an atomic failure.
enum class boundary_overflow : std::uint8_t
{
    allow, ///< Preserve sizes even outside the parent content rectangle; no clipping is implied.
    reject ///< Fail the entire layout when this axis's outer bounds cannot fit.
};

/// @brief One directed outer-edge relation, authored on the dependent element.
struct boundary_relation
{
    element_handle source; ///< Exact live sibling identity, never an unvalidated index.
    boundary_side side = boundary_side::after; ///< Source edge and target edge pairing.
    float gap = 0; ///< Finite nonnegative separation between outer margin edges.
    boundary_sizing sizing = boundary_sizing::desired;         ///< Explicit allocation policy.
    boundary_overflow overflow = boundary_overflow::allow;     ///< Incompatible bounds policy.
    bool operator==(const boundary_relation&) const = default; ///< Full dependency identity.
};

/// @brief At most one incoming relationship per axis; both axes share an acyclic graph.
struct layout_boundaries
{
    std::optional<boundary_relation> horizontal; ///< Replaces horizontal canvas-axis placement.
    std::optional<boundary_relation> vertical;   ///< Replaces vertical canvas-axis placement.
    bool operator==(const layout_boundaries&) const = default; ///< Empty restores canvas policy.
};

/// @brief Snapshot entry in ascending dependent live-slot order.
struct boundary_record
{
    element_handle target;        ///< Dependent identity at capture.
    layout_boundaries boundaries; ///< Sparse immutable relationship values.
};
} // namespace atlas::ui
