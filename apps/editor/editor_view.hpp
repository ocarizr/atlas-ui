/** @file @brief Atlas UI editor chrome, input routing and immutable render handoff. */
#pragma once
#include "document_session.hpp"
#include <atlas/ui_text/paint.hpp>

namespace atlas::editor
{
/// @brief A retained render packet contains only portable CPU values.
struct editor_frame final : render_host::frame_payload
{
    ui::layout_rect viewport;          ///< Full editor viewport in DIPs.
    std::vector<ui::paint_quad> quads; ///< Ordered chrome, preview, and label draws.
    std::shared_ptr<const ui_text::glyph_atlas> atlas; ///< Immutable masks shared across frames.
};

/// @brief Main-thread composition and editor control dispatch.
class editor_view
{
  public:
    /// @brief Retain one authored session and its selected file.
    editor_view(document_session& session, jobs::job_system& jobs, std::filesystem::path file,
                const diagnostics::services& sinks);
    ~editor_view(); ///< Retire UI and input on the main owner.
    /// @brief Publish an updated immutable packet after dispatching native input.
    [[nodiscard]] std::shared_ptr<const editor_frame>
    snapshot(const window::window_state& state, std::span<const input::event> events);

  private:
    class impl;                  ///< Editor panels and transient runtime state.
    std::unique_ptr<impl> impl_; ///< Composition details stay in the editor application.
};
} // namespace atlas::editor
