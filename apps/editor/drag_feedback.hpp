/** @file @brief Small render-only drag marker with accepted, pending and rejected feedback. */
#pragma once
#include "editor_view.hpp"

namespace atlas::editor
{
/// @brief The marker is appended after chrome and never enters the UI tree or capture model.
inline ui::paint_quad drag_marker(const ui::drag_feedback& feedback, ui::layout_rect viewport)
{
    const ui::layout_rect bounds{feedback.position.x + 12, feedback.position.y + 12, 18, 18};
    const std::array<float, 4> color = feedback.phase == ui::drag_phase::pending
                                           ? std::array<float, 4>{0.9F, 0.65F, 0.15F, 0.9F}
                                       : feedback.operation == ui::drag_operation::none
                                           ? std::array<float, 4>{0.8F, 0.15F, 0.15F, 0.9F}
                                           : std::array<float, 4>{0.15F, 0.7F, 0.35F, 0.9F};
    const ui::paint_vertex a{{bounds.x, bounds.y}, {}, color};
    const ui::paint_vertex b{{bounds.x + bounds.width, bounds.y}, {}, color};
    const ui::paint_vertex c{{bounds.x, bounds.y + bounds.height}, {}, color};
    const ui::paint_vertex d{{bounds.x + bounds.width, bounds.y + bounds.height}, {}, color};
    return {{}, bounds, viewport, 0, {a, b, c, c, b, d}};
}
} // namespace atlas::editor
