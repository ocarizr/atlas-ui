/** @file @brief Owner-thread authored panel with a bounded Lua-to-editor command mailbox. */
#pragma once
#include "panel_scene.hpp"
#include "tool_registry.hpp"

namespace atlas::editor
{
/// @brief Results become visible only after a successful authored input phase.
struct tool_input_result
{
    std::vector<control_action> commands; ///< Editor commands produced by Lua.
    bool changed = false;                 ///< A layout or paint property was updated.
};

/// @brief Retain a private runtime instance for one authored tool.
class tool_panel
{
  public:
    /// @brief Install scoped editor capabilities before constructing Lua instances.
    tool_panel(const tool_entry& source, bool shell, const diagnostics::services& sinks);
    ~tool_panel(); ///< Retire behavior and its mailbox together on the owner.
    [[nodiscard]] ui::layout_rect area(std::string_view id) const; ///< Arranged named shell slot.
    void append_labels(std::vector<control>& target) const; ///< Append authored text overlays.
    /// @brief Recalculate and paint against the selected viewport.
    [[nodiscard]] ui::paint_snapshot paint(ui::layout_rect bounds, ui::layout_engine& layout,
                                           ui::paint_engine& painter);
    /// @brief Route one serial input phase and publish its owned results.
    [[nodiscard]] tool_input_result dispatch(std::span<const input::event> events, float density);

  private:
    class mailbox;                                ///< Scoped C++ editor capability exposed to Lua.
    std::shared_ptr<mailbox> mailbox_;            ///< Outlives the behavior instance.
    std::unique_ptr<ui_content::instance> view_;  ///< The independent live tool.
    ui::element_snapshot source_;                 ///< Input source matching geometry.
    ui::layout_snapshot geometry_;                ///< Frozen geometry for hit testing.
    application::ui_input_batch bridge_{nullptr}; ///< Shared input adapter.
    std::uint64_t frame_ = 1; ///< The content runtime reserves bootstrap frame one.
    std::vector<std::pair<ui::element_handle, std::string>> labels_; ///< Authored captions.
};
} // namespace atlas::editor
