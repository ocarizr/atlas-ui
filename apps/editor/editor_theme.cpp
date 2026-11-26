/** @file @brief Complete editor swatches and transactional user overrides. */
#include "editor_theme.hpp"
#include <algorithm>
#include <limits>

namespace atlas::editor
{
/// @brief Keep native and YAML candidates unique and deterministic without node allocations.
void normalize_theme_overrides(std::vector<theme_override>& values)
{
    if (values.size() > ui::max_palette_colors)
    {
        throw std::invalid_argument("Too many editor theme overrides");
    }

    std::ranges::sort(values, {}, &theme_override::key);
    if (std::ranges::adjacent_find(values, {}, &theme_override::key) != values.end())
    {
        throw std::invalid_argument("Duplicate editor theme override");
    }
}

/// @brief Centralized presets are native definitions using the same runtime palette compiler.
std::shared_ptr<const ui::palette> editor_preset(std::string_view name)
{
    if (name != "dark" && name != "light")
    {
        throw std::invalid_argument("Unknown editor theme preset");
    }

    const bool light = name == "light";
    return ui::palette::compile(
        {"atlas.editor.v1",
         {{"background", ui_content::parse_color(light ? "#D4CABAFF" : "#17191DFF")},
          {"surface", ui_content::parse_color(light ? "#EEE6D8FF" : "#26292FFF")},
          {"control", ui_content::parse_color(light ? "#DCD0BBFF" : "#383D46FF")},
          {"accent", ui_content::parse_color(light ? "#A36D25FF" : "#BD8A45FF")},
          {"text", ui_content::parse_color(light ? "#29251FFF" : "#EFE8DCFF")}}});
}

namespace
{
/// @brief Validate every override before constructing a compatible complete replacement.
std::shared_ptr<const ui::palette> resolve(const theme_preferences& source)
{
    auto definition = editor_preset(source.preset)->definition();
    for (const auto& [key, hex] : source.overrides)
    {
        const auto found = std::ranges::find(definition.colors, key, &ui::palette_color::key);
        if (found == definition.colors.end())
        {
            throw std::out_of_range("Unknown editor swatch key: " + key);
        }

        found->value = ui_content::parse_color(hex);
    }

    return ui::palette::compile(std::move(definition));
}
} // namespace

/// @brief The chrome context is separate from every document or Flow preview context.
editor_theme::editor_theme(diagnostics::services sinks)
    : context_{std::make_shared<ui::palette_context>(editor_preset("dark"), std::move(sinks))}
{
}

/// @brief A failed durable save cannot leave a displayed theme that will disappear on restart.
void editor_theme::configure(theme_preferences candidate)
{
    const auto selected = context_->snapshot();
    normalize_theme_overrides(candidate.overrides);
    const auto compiled = resolve(candidate);
    if (!selected->compatible(*compiled) ||
        context_->revision() == std::numeric_limits<std::uint64_t>::max())
    {
        throw std::invalid_argument("Editor theme cannot accept a new revision");
    }

    const auto yaml = write_theme_preferences(candidate);
    if (save_)
    {
        save_(yaml);
    }

    context_->select(compiled);
    preferences_ = std::move(candidate);
}

/// @brief Persistence is supplied by the host after startup preferences have been read.
void editor_theme::persist(std::function<void(std::string_view)> save)
{
    save_ = std::move(save);
}

/// @brief Return only the committed user choices.
const theme_preferences& editor_theme::preferences() const noexcept
{
    return preferences_;
}

/// @brief Consumers share only an explicitly selected chrome context.
const std::shared_ptr<ui::palette_context>& editor_theme::context() const noexcept
{
    return context_;
}
} // namespace atlas::editor
