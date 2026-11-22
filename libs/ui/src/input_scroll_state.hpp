/** @file @brief Bounded per-frame scroll requests coalesce before command publication. */
#pragma once
#include "paint_transform.hpp"
#include <atlas/ui/input_session.hpp>
#include <vector>

namespace atlas::ui
{
/// @brief One pending viewport offset, separate from frozen paint/input geometry.
struct pending_scroll
{
    element_handle target;    ///< Captured viewport identity.
    scroll_geometry geometry; ///< Frozen extent, clip and starting offset.
    layout_point offset;      ///< Coalesced finite request for the end-of-frame command boundary.
    bool requested = false;   ///< Only actual input/anchor requests replace other producers.
};

/// @brief Retain only the current immutable inputs and sparse mutable offsets.
struct scroll_input_frame
{
    scroll_input_frame(const element_snapshot& input, const layout_snapshot& layout,
                       std::pmr::memory_resource* memory)
        : source(input), geometry(layout), pending(memory)
    {
        pending.reserve(layout.scrolls().size());
        for (const auto& item : layout.scrolls())
        {
            pending.push_back({item.target, item.geometry, item.geometry.offset});
        }
    } ///< All allocation happens before command-frame admission.

    element_snapshot source;  ///< Shared immutable source, never a live tree pointer.
    layout_snapshot geometry; ///< Frozen geometry shared with painting.
    std::pmr::vector<pending_scroll> pending; ///< At most one request per scroll owner.
    element_handle focus{}; ///< Focus anchor at the last successful command-production boundary.
};

/// @brief A single captured thumb drag; geometry changes cancel rather than rebase it silently.
struct scroll_drag
{
    element_handle target;             ///< Captured chrome element.
    scrollbar_geometry geometry;       ///< Frozen track/thumb/range identity at the press.
    float press = 0;                   ///< Pointer coordinate in the untransformed track space.
    float offset = 0;                  ///< Effective offset at press.
    detail::paint_transform transform; ///< Presentation changes cancel a captured drag.
    float range = 0;                   ///< Range at press; resizing/content changes cancel capture.
};
} // namespace atlas::ui
