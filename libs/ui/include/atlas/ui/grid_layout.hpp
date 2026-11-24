/**
 * @file
 * @brief Immutable grid track policies and sparse parent-owned cell placement.
 */
#pragma once
#include <atlas/ui/layout_types.hpp>
#include <optional>
#include <vector>

namespace atlas::ui
{
/// @brief Grid sizing is independent of whether children share a data-driven template.
enum class grid_unit : std::uint8_t
{
    automatic, ///< Natural child contributions determine the track size.
    fixed,     ///< Reserve an explicit nonnegative logical size.
    fraction   ///< Natural size plus a weighted share of positive remaining space.
};

/// @brief A compact track value; automatic tracks use the canonical value zero.
struct grid_track
{
    grid_unit unit = grid_unit::automatic; ///< Validated sizing policy.
    float value = 0; ///< Fixed size, positive fraction weight, or zero for automatic.
    bool operator==(const grid_track&) const = default; ///< Exact geometry input equality.
};

/// @brief At most one implicit axis; both explicit axes describe a fixed cell topology.
struct grid_definition
{
    std::vector<grid_track> columns{{grid_unit::fraction, 1}}; ///< Empty means implicit columns.
    std::vector<grid_track> rows{}; ///< Empty means implicit automatic rows.
    float column_gap = 0;           ///< Finite nonnegative spacing, including internal span gaps.
    float row_gap = 0;              ///< Finite nonnegative spacing.
    bool operator==(const grid_definition&) const = default; ///< Compare actual track values.
};

/// @brief Cheap shared immutable policy copies keep vector storage out of every layout node.
class grid_layout
{
  public:
    grid_layout() = default; ///< One weighted column with implicit automatic rows.
    explicit grid_layout(grid_definition definition); ///< Validate before retaining immutable data.
    const grid_definition& definition() const;        ///< Stable borrowed immutable track input.
    bool
    operator==(const grid_layout& other) const; ///< Exact value equality, with identity fast path.
  private:
    std::shared_ptr<const grid_definition>
        definition_; ///< Shared source data, never mutable tracks.
};

/// @brief Explicit zero-based cell coordinates, independent of physical element slots.
struct grid_cell
{
    std::uint32_t row = 0;                             ///< Logical row.
    std::uint32_t column = 0;                          ///< Logical column.
    bool operator==(const grid_cell&) const = default; ///< Compare both coordinates.
};

/// @brief Missing position uses stable sparse auto-placement around explicit occupied cells.
struct grid_slot
{
    std::optional<grid_cell>
        position{};                ///< Set both coordinates together, or use automatic placement.
    std::uint32_t row_span = 1;    ///< Positive number of rows.
    std::uint32_t column_span = 1; ///< Positive number of columns.
    bool operator==(const grid_slot&) const = default; ///< Exact authored metadata equality.
};

/// @brief Sparse authored child metadata retained by immutable snapshots.
struct grid_slot_record
{
    element_handle target; ///< Full child identity.
    grid_slot slot;        ///< Parent-specific cell placement.
};

/// @brief Validate local coordinates/spans against the parent's explicit and implicit axes.
void validate_grid_slot(const grid_slot& slot, const grid_layout& parent);
/// @brief Resolve stable sparse auto-placement atomically; input/output spans may alias.
void resolve_grid_slots(const grid_layout& parent, std::span<const grid_slot> authored,
                        std::span<grid_slot> output,
                        std::pmr::memory_resource* memory = std::pmr::get_default_resource());
} // namespace atlas::ui
