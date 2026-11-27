/** @file @brief Recolor cached chrome without rebuilding layout, input or glyph masks. */
#include "panel_scene.hpp"
#include <algorithm>

namespace atlas::editor
{
/// @brief Replace only chrome-owned ranges; document and Flow preview payloads stay untouched.
void panel_scene::recolor(std::shared_ptr<const ui::palette> palette, chrome_paint_sources sources,
                          ui::paint_engine& painter)
{
    if (!palette || !palette_ || !palette_->compatible(*palette))
    {
        throw std::invalid_argument("Incompatible chrome palette");
    }

    for (const auto& [target, slot] : colors_)
    {
        auto style = tree_.paint(target);
        style.color = palette->color(slot);
        tree_.set_paint(target, style);
    }

    source_ = tree_.snapshot();
    const auto native = painter.calculate(source_, geometry_);
    const auto old = std::span{packet_->quads};
    const auto middle =
        tool_visible_ ? sources.tool : old.subspan(tool_begin_, text_begin_ - tool_begin_);
    const auto labels = old.subspan(text_begin_);
    auto candidate = std::make_shared<editor_frame>();
    candidate->viewport = packet_->viewport;
    candidate->atlas = packet_->atlas;
    candidate->base = packet_->base;
    candidate->drag = packet_->drag;
    candidate->quads.reserve(sources.shell.size() + native.quads().size() + middle.size() +
                             labels.size());
    const auto append = [&](std::span<const ui::paint_quad> range)
    {
        candidate->quads.insert(candidate->quads.end(), range.begin(), range.end());
    };
    append(sources.shell);
    append(native.quads());
    const auto tool_begin = candidate->quads.size();
    append(middle);
    const auto text_begin = candidate->quads.size();
    append(labels);
    const auto text = palette->color(palette->find("text"));
    for (auto& quad : std::span{candidate->quads}.subspan(text_begin))
    {
        for (auto& vertex : quad.vertices)
        {
            vertex.color = text;
        }
    }

    tool_begin_ = tool_begin;
    text_begin_ = text_begin;
    packet_ = std::move(candidate);
    palette_ = std::move(palette);
}
} // namespace atlas::editor
