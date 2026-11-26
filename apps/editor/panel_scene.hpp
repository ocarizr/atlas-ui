/** @file @brief Compose the editor panels and route input through an Atlas UI tree. */
#pragma once
#include "editor_view.hpp"
#include "input_adapter.hpp"
#include "panels.hpp"

namespace atlas::editor
{
/// @brief Named authored paint ranges prevent exchanging shell and tool contributions.
struct chrome_paint_sources
{
    std::span<const ui::paint_quad> shell; ///< Current shell, including alpha culling.
    std::span<const ui::paint_quad> tool;  ///< Current optional tool contribution.
};

/// @brief One main-thread panel layout and its input registrations.
class panel_scene
{
  public:
    /// @brief Build chrome and append the isolated preview before producing the render packet.
    panel_scene(const panel_plan& plan, ui::element_tree& preview, ui::layout_rect viewport,
                float scale, ui::layout_engine& layout, ui::paint_engine& paint,
                const diagnostics::services& sinks, std::optional<control_action> focus = {});
    /// @brief Borrow the immutable presentation packet.
    [[nodiscard]] const std::shared_ptr<const editor_frame>& packet() const noexcept;
    /// @brief Return actions after all routing for the input batch has finished.
    [[nodiscard]] std::vector<control_action> dispatch(std::span<const input::event> events);

    std::span<const ui::input_event>
    begin_input(std::span<const input::event> events); ///< Open for shared-window routing.
    std::vector<control_action> finish_input(); ///< Close after the drag domain releases its lease.
    ui::input_session& input() noexcept; ///< Checked domain participation, no ownership transfer.
    control_action destination(ui::element_handle target) const; ///< Resolve a native drop target.
    std::optional<control_action> focused_action() const; ///< Bookmark a logical chrome command.
    void recolor(std::shared_ptr<const ui::palette> palette, chrome_paint_sources sources,
                 ui::paint_engine& painter); ///< Reuse layout.

  private:
    std::vector<std::pair<ui::element_handle, ui::palette_slot>>
        colors_;                 ///< Resolved chrome slots.
    std::size_t tool_begin_ = 0; ///< Optional tool start.
    bool tool_visible_ = false;  ///< A tool owns the middle range even when transparent.
    std::size_t text_begin_ = 0; ///< Chrome label suffix.
    std::shared_ptr<const ui::palette> palette_; ///< Exact chrome contract and previous colors.
    std::vector<std::pair<ui::element_handle, control_action>>
        destinations_;                    ///< Native transaction routing.
    std::vector<control_action> pending_; ///< Short-lived actions emitted by input handlers.
    std::vector<std::pair<ui::element_handle, control_action>>
        focus_targets_;                           ///< Current controls.
    std::optional<control_action> initial_focus_; ///< Restore the logical command after rebuilding.
    ui::element_tree tree_;                       ///< Main-thread chrome only.
    ui::input_session input_;                     ///< Control lifetime ends before the chrome tree.
    ui::element_snapshot source_;                 ///< Matching input state.
    ui::layout_snapshot geometry_;                ///< Frozen chrome geometry for hit testing.
    std::shared_ptr<const editor_frame> packet_;  ///< Retained by either thread.
    application::ui_input_batch input_bridge_{nullptr}; ///< Reusable normalized input scratch.
    float scale_;                                       ///< Physical pixels per logical DIP.
    std::uint64_t frame_ = 0;                           ///< Scoped to this chrome input lifetime.
};
} // namespace atlas::editor
