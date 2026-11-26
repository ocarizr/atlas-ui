/** @file @brief Bounded swatch controls and private text drafts for editor preferences. */
#pragma once
#include "editor_theme.hpp"
#include "panels.hpp"
#include <atlas/input/text_entry.hpp>

namespace atlas::editor
{
/// @brief Customization uses regular Atlas controls; invalid drafts never change the theme.
class theme_panel
{
  public:
    bool visible = false; ///< Dedicated inspector page, independent from project content.
    void append(panel_plan& plan, const editor_theme& theme); ///< Show preset and swatch controls.
    void execute(std::string_view action, editor_theme& theme); ///< Run after input dispatch.
    bool input(std::span<const input::event> events);           ///< Consume active draft keys only.
  private:
    std::size_t key_ = 0;        ///< Stable index in the complete editor contract.
    input::text_entry draft_{9}; ///< Exact bounded #RRGGBBAA draft.
    bool editing_ = false;       ///< Suspend ordinary keyboard routing while editing.
    std::string status_;         ///< Local validation result.
};
} // namespace atlas::editor
