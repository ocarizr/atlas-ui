/** @file @brief Editor-only swatch presets and user preferences, independent of preview content. */
#pragma once
#include <atlas/ui_content/palette.hpp>
#include <functional>
#include <map>

namespace atlas::editor
{
/// @brief YAML stores only a selected immutable preset and explicit semantic overrides.
struct theme_preferences
{
    std::string preset = "dark";                               ///< Built-in dark or light preset.
    std::map<std::string, std::string, std::less<>> overrides; ///< Key to canonical sRGB8 hex.
};

[[nodiscard]] std::shared_ptr<const ui::palette>
editor_preset(std::string_view name); ///< Native complete palettes share one semantic contract.
[[nodiscard]] theme_preferences parse_theme_preferences(std::string_view yaml); ///< Strict YAML.
[[nodiscard]] std::string
write_theme_preferences(const theme_preferences& value); ///< Validate first.

/// @brief Owner-only transaction: validate, persist through host callback, then select.
class editor_theme
{
  public:
    explicit editor_theme(
        diagnostics::services sinks = {});       ///< Start with an immutable dark preset.
    void configure(theme_preferences candidate); ///< Failed parsing/save preserves prior state.
    void persist(std::function<void(std::string_view)> save); ///< Inject the editor write boundary.
    [[nodiscard]] const theme_preferences& preferences() const noexcept; ///< Last accepted values.
    [[nodiscard]] const std::shared_ptr<ui::palette_context>&
    context() const noexcept; ///< Chrome only.
  private:
    theme_preferences preferences_; ///< Preset values are never mutated in place.
    std::shared_ptr<ui::palette_context>
        context_; ///< Every editor view can share one owner context.
    std::function<void(std::string_view)>
        save_; ///< No direct storage access from theme technology.
};
} // namespace atlas::editor
