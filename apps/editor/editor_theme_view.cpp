/** @file @brief Theme publication retains preview state and chrome focus identities. */
#include "editor_view_state.hpp"

namespace atlas::editor
{
/// @brief Repaint authored chrome using its existing instance and cached layout geometry.
void editor_view::impl::refresh_theme()
{
    if (!chrome || theme_revision == theme.context()->revision())
    {
        return;
    }

    const auto shell_paint =
        shell_visible ? shell->paint(viewport, layout, paint) : ui::paint_snapshot{};
    const auto tool_paint = active_tool && !(assets && assets->visible) && !(flow && flow->visible)
                                ? active_tool->paint(tool_bounds, layout, paint)
                                : ui::paint_snapshot{};
    chrome->recolor(theme.context()->snapshot(),
                    {.shell = shell_paint.quads(), .tool = tool_paint.quads()}, paint);
    theme_revision = theme.context()->revision();
}
} // namespace atlas::editor
