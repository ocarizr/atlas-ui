/** @file @brief Compose the editor panels and route input through an Atlas UI tree. */
#pragma once
#include "editor_view.hpp"
#include "input_adapter.hpp"
#include "panels.hpp"

namespace atlas::editor
{
/// @brief One main-thread panel layout and its input registrations.
class panel_scene
{
  public:
    /// @brief Build chrome and append the isolated preview before producing the render packet.
    panel_scene(const panel_plan& plan, ui::element_tree& preview, ui::layout_rect viewport,
                float scale, ui::layout_engine& layout, ui::paint_engine& paint,
                const diagnostics::services& sinks);
    /// @brief Borrow the immutable presentation packet.
    [[nodiscard]] const std::shared_ptr<const editor_frame>& packet() const noexcept;
    /// @brief Return actions after all routing for the input batch has finished.
    [[nodiscard]] std::vector<control_action> dispatch(std::span<const input::event> events);

  private:
    std::vector<control_action> pending_;        ///< Short-lived actions emitted by input handlers.
    ui::element_tree tree_;                      ///< Main-thread chrome only.
    ui::input_session input_;                    ///< Control lifetime ends before the chrome tree.
    ui::element_snapshot source_;                ///< Matching input state.
    ui::layout_snapshot geometry_;               ///< Frozen chrome geometry for hit testing.
    std::shared_ptr<const editor_frame> packet_; ///< Retained by either thread.
    application::ui_input_batch input_bridge_{nullptr}; ///< Reusable normalized input scratch.
    float scale_;                                       ///< Physical pixels per logical DIP.
    std::uint64_t frame_ = 0;                           ///< Scoped to this chrome input lifetime.
};
} // namespace atlas::editor
