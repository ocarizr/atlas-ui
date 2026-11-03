/** @file @brief Invalid enum, spacing and overlapping placement policies fail before mutation. */
#include "boundary_validation.hpp"
#include <cmath>
#include <stdexcept>

namespace atlas::ui::detail
{
namespace
{
/// @brief One relationship replaces exactly one default canvas axis.
void validate_axis(const std::optional<boundary_relation>& value, const canvas_axis& slot)
{
    if (!value)
    {
        return;
    }

    if (!std::isfinite(value->gap) || value->gap < 0 ||
        (value->side != boundary_side::before && value->side != boundary_side::after) ||
        (value->sizing != boundary_sizing::desired &&
         value->sizing != boundary_sizing::remaining) ||
        (value->overflow != boundary_overflow::allow &&
         value->overflow != boundary_overflow::reject))
    {
        throw std::invalid_argument("invalid UI boundary policy or gap");
    }

    if (slot != canvas_axis{})
    {
        throw std::invalid_argument("UI boundary conflicts with canvas axis placement");
    }
}
} // namespace

/// @brief Both axes validate without publishing a partial relationship.
void validate_boundary_values(const layout_boundaries& value, const canvas_slot& slot)
{
    validate_axis(value.horizontal, slot.horizontal);
    validate_axis(value.vertical, slot.vertical);
}
} // namespace atlas::ui::detail
