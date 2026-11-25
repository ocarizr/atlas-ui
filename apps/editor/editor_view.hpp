/** @file @brief Atlas UI editor chrome, input routing and immutable render handoff. */
#pragma once
#include "document_session.hpp"
#include "document_workspace.hpp"
#include <atlas/editor_flow/persistence.hpp>
#include <atlas/ui_text/paint.hpp>

namespace atlas::editor
{
class tool_registry;    ///< Optional editor and project contributions.
class asset_repository; ///< Optional writable editor content origin.

/// @brief A retained render packet contains only portable CPU values.
struct editor_frame final : render_host::frame_payload
{
    ui::layout_rect viewport;          ///< Full editor viewport in DIPs.
    std::vector<ui::paint_quad> quads; ///< Ordered chrome, preview, and label draws.
    std::shared_ptr<const ui_text::glyph_atlas> atlas; ///< Immutable masks shared across frames.
    std::shared_ptr<const editor_frame> base{};        ///< Share unchanged chrome while dragging.
    std::optional<ui::drag_feedback> drag{}; ///< Render-only feedback; never a hit-test target.
};

/// @brief Main-thread composition and editor control dispatch.
class editor_view
{
  public:
    /// @brief Retain one authored session and its selected file.
    editor_view(document_session& session, jobs::job_system& jobs, std::filesystem::path file,
                const diagnostics::services& sinks, const tool_registry* tools = nullptr);
    /// @brief Bind lifecycle commands to the active workspace session.
    editor_view(document_workspace& workspace, jobs::job_system& jobs,
                const diagnostics::services& sinks, const tool_registry* tools = nullptr);
    /// @brief Save every dirty session to recovery; failure keeps the editor open.
    [[nodiscard]] bool preserve();
    /// @brief Checkpoint every 30 seconds using the supplied monotonic owner time.
    void autosave(std::chrono::steady_clock::time_point now);
    /// @brief Attach type-oriented asset authoring before the first frame.
    void bind_assets(asset_repository& repository, std::filesystem::path root);
    /// @brief Attach an independently owned Flow document before routing graph gestures.
    void bind_flow(editor_flow::persistence& document, asset_repository* repository = nullptr,
                   std::shared_ptr<std::pmr::memory_resource> memory = {},
                   const std::string& identity_scope = {});
    /// @brief Discover typed actions/conditions from streamer-supplied immutable catalog sources.
    void bind_flow_module(std::span<const scripting::source_asset> sources,
                          scripting::callable_module entry);
    /// @brief Attach the retained typed mount used only by explicit isolated Flow preview.
    void bind_flow_preview(std::shared_ptr<streaming::catalog_streamer> storage,
                           std::shared_ptr<std::pmr::memory_resource> memory = {});
    ~editor_view(); ///< Retire UI and input on the main owner.
    /// @brief Publish an updated immutable packet after dispatching native input.
    [[nodiscard]] std::shared_ptr<const editor_frame>
    snapshot(const window::window_state& state, std::span<const input::event> events);

  private:
    class impl;                  ///< Editor panels and transient runtime state.
    std::unique_ptr<impl> impl_; ///< Composition details stay in the editor application.
};
} // namespace atlas::editor
