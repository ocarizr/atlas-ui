/** @file @brief Color source controls use document context keys and one undoable transaction. */
#include "color_inspector.hpp"
#include <algorithm>

namespace atlas::editor
{
/// @brief Reference selection does not bake the editor's current theme into source values.
void append_color_controls(panel_plan& plan, const document_session& session)
{
    const auto keys = session.color_keys();
    if (keys.empty() || plan.inspector.height < 280)
    {
        return;
    }

    const auto target = std::ranges::find(session.document().elements, session.selected(),
                                          &ui_content::element::id);
    const auto add = [&](float offset, const std::string& key, bool material)
    {
        plan.controls.push_back(
            {{plan.inspector.x + 8, plan.inspector.y + plan.inspector.height - offset,
              plan.inspector.width - 16, 24},
             {.14F, .21F, .30F, 1},
             std::string{material ? "Material tint: " : "Paint tint: "} +
                 (key.empty() ? "literal" : key),
             {control_kind::color_source, material ? "material" : "paint"}});
    };
    add(86, target->color_key, false);
    if (target->material || target->paint.material)
    {
        add(114, target->material_color_key, true);
    }
}

/// @brief Preserve literal data when selecting a key; the final selection restores literal mode.
void cycle_color_source(document_session& session, bool material)
{
    const auto keys = session.color_keys();
    const auto target = std::ranges::find(session.document().elements, session.selected(),
                                          &ui_content::element::id);
    const auto& current = material ? target->material_color_key : target->color_key;
    const auto found = std::ranges::find(keys, current);
    const auto next = found == keys.end() ? keys.begin() : found + 1;
    (void)session.set_color_key(next == keys.end() ? std::string{} : *next, material);
}
} // namespace atlas::editor
