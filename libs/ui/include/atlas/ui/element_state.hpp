/**
 * @file
 * @brief Local requested state and copied hierarchy records, independent of live registry storage.
 */
#pragma once
#include <atlas/ui/commands.hpp>

namespace atlas::ui
{
/// @brief Committed local requests; no computed layout or inherited visibility is evaluated.
struct requested_properties
{
    float width = 0.f;   ///< Finite nonnegative requested width.
    float height = 0.f;  ///< Finite nonnegative requested height.
    float opacity = 1.f; ///< Local opacity in [0, 1].
    bool visible = true; ///< Local visibility, independent of parent visibility and culling.
    bool operator==(const requested_properties&) const = default; ///< Compare complete requests.
};

/// @brief Conservative pending work flags; acknowledgment is explicit and owner-only.
struct invalidation
{
    bool layout = true; ///< Geometry-affecting state or hierarchy has changed.
    bool paint = true;  ///< Visual state or hierarchy has changed.
};

/// @brief Copied per-element input; handles identify relationships without borrowing components.
struct element_record
{
    element_handle target;          ///< Session, slot and generation at capture time.
    element_handle parent;          ///< Empty for an independent root.
    element_handle first_child;     ///< First child in insertion order, or empty.
    element_handle next_sibling;    ///< Next child of the same parent, or empty.
    requested_properties requested; ///< Committed local scalar requests at capture time.
    invalidation dirty;             ///< Local work flags at capture time.
};
} // namespace atlas::ui
