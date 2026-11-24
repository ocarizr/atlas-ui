/** @file @brief Atomic portable grid occupancy resolution shared by runtime and authoring. */
#include "grid_validation.hpp"
#include <algorithm>

namespace atlas::ui
{
namespace
{
using detail::max_grid_cells; // Shared bounded topology contract.

/// @brief One panel's sparse occupancy; implicit columns transpose the linear scan.
class grid_occupancy
{
  public:
    /// @brief Borrow the validated fixed axis and allocate occupancy only when cells are reserved.
    grid_occupancy(const grid_definition& definition, std::pmr::memory_resource* memory)
        : policy(definition), column_major(definition.columns.empty()),
          stride(column_major ? definition.rows.size() : definition.columns.size()), bits(0, memory)
    {
    }

    grid_cell cell(std::size_t ordinal) const
    {
        const auto major = static_cast<std::uint32_t>(ordinal / stride);
        const auto minor = static_cast<std::uint32_t>(ordinal % stride);
        return column_major ? grid_cell{minor, major} : grid_cell{major, minor};
    } ///< Translate a stable linear admission index into logical row/column coordinates.

    bool fits(const grid_slot& slot)
    {
        const auto position = slot.position.value_or(grid_cell{});
        const auto rows = std::uint64_t{position.row} + slot.row_span;
        const auto columns = std::uint64_t{position.column} + slot.column_span;
        if ((!policy.rows.empty() && rows > policy.rows.size()) ||
            (!policy.columns.empty() && columns > policy.columns.size()) ||
            (column_major ? columns : rows) * stride > max_grid_cells)
        {
            return false;
        }

        for (std::uint32_t row = 0; row < slot.row_span; ++row)
        {
            for (std::uint32_t column = 0; column < slot.column_span; ++column)
            {
                if (++checks > std::size_t{max_grid_cells} * 64)
                {
                    throw std::overflow_error("grid placement work admission exhausted");
                }

                if (occupied(position.row + row, position.column + column))
                {
                    return false;
                }
            }
        }

        return true;
    } ///< No bit allocation occurs while probing a candidate.

    void occupy(const grid_slot& slot)
    {
        if (!fits(slot))
        {
            throw std::invalid_argument("overlapping or out-of-range grid cells");
        }

        const auto position = slot.position.value_or(grid_cell{});
        for (std::uint32_t row = 0; row < slot.row_span; ++row)
        {
            for (std::uint32_t column = 0; column < slot.column_span; ++column)
            {
                const auto index = ordinal(position.row + row, position.column + column);
                if (index / 64 >= bits.size())
                {
                    const auto required = index / 64 + 1;
                    if (required > bits.capacity())
                    {
                        bits.reserve(std::min(std::size_t{max_grid_cells / 64},
                                              std::max(required, bits.capacity() * 2)));
                    }

                    bits.resize(index / 64 + 1, 0);
                }

                bits[index / 64] |= std::uint64_t{1} << (index % 64);
            }
        }
    } ///< Grow only the necessary part of a maximum 8 KiB occupancy map.
  private:
    /// @brief Convert coordinates after topology bounds have been checked.
    std::size_t ordinal(std::uint32_t row, std::uint32_t column) const
    {
        return column_major ? std::size_t{column} * stride + row
                            : std::size_t{row} * stride + column;
    }

    ///< Convert coordinates after topology bounds have been checked.
    bool occupied(std::uint32_t row, std::uint32_t column) const
    {
        const auto index = ordinal(row, column);
        return index / 64 < bits.size() && (bits[index / 64] & (std::uint64_t{1} << (index % 64)));
    } ///< Bits beyond allocated scratch are implicitly empty.

    const grid_definition& policy;        ///< Borrowed immutable source definition.
    bool column_major;                    ///< Exactly one axis may be implicit.
    std::size_t stride;                   ///< Positive fixed-axis count.
    std::pmr::vector<std::uint64_t> bits; ///< Discarded before worker admission.
    std::size_t checks = 0; ///< Bound pathological sparse spanning searches as well as storage.
};

} // namespace

/// @brief Explicit reservations precede sparse automatic placement, with all-or-nothing output.
void resolve_grid_slots(const grid_layout& parent, std::span<const grid_slot> authored,
                        std::span<grid_slot> output, std::pmr::memory_resource* memory)
{
    if (!memory || authored.size() != output.size() || authored.size() > max_grid_cells)
    {
        throw std::invalid_argument("invalid grid placement output or admission");
    }

    const auto& policy = parent.definition();
    grid_occupancy occupancy{policy, memory};
    std::pmr::vector<grid_slot> prepared(authored.begin(), authored.end(), memory);
    for (const auto& slot : prepared)
    {
        validate_grid_slot(slot, parent);
        if (slot.position)
        {
            occupancy.occupy(slot);
        }
    }

    std::size_t cursor = 0;
    for (auto& slot : prepared)
    {
        if (slot.position)
        {
            continue;
        }

        do
        {
            if (cursor >= max_grid_cells)
            {
                throw std::overflow_error("grid cell admission exhausted");
            }

            slot.position = occupancy.cell(cursor++);
        } while (!occupancy.fits(slot));

        occupancy.occupy(slot);
    }

    std::copy(prepared.begin(), prepared.end(), output.begin());
}
} // namespace atlas::ui
