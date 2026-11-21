/** @file @brief Atlas Flow graph tool composed from portable authoring and shared editor services.
 */
#pragma once
#include "asset_repository.hpp"
#include "input_adapter.hpp"
#include "panels.hpp"
#include <atlas/editor_flow/canvas.hpp>
#include <atlas/editor_flow/inspector.hpp>
#include <atlas/editor_flow/persistence.hpp>
#include <atlas/editor_flow/preview.hpp>
#include <map>

namespace atlas::editor
{
/// @brief Graph tool owns transient UI state; persistent graphs remain independently owned.
class flow_panel
{
  public:
    /// @brief Borrow persistent owners; optional identity scope supports deterministic host
    /// fixtures.
    flow_panel(editor_flow::persistence& document, diagnostics::services sinks,
               asset_repository* repository = nullptr,
               std::shared_ptr<std::pmr::memory_resource> memory = {},
               const std::string& identity_scope = {});
    void
    append(panel_plan& plan, ui::layout_engine& layout, ui::paint_engine& paint,
           const document_session& ui); ///< Render graph and paginated typed inspector with Atlas.
    bool input(std::span<const input::event> events, float scale); ///< Owner input, no native SDK.
    bool execute(const control_action& command, document_session& ui); ///< Apply after callbacks.
    void checkpoint(); ///< Preserve the graph independently from UI documents.
    /// @brief Retain an immutable typed mount; explicit apply snapshots callable registrations.
    void bind_preview(const jobs::job_dispatcher& jobs,
                      std::shared_ptr<streaming::catalog_streamer> storage,
                      std::shared_ptr<std::pmr::memory_resource> memory = {});
    bool pump_preview(std::span<const input::event> events,
                      float density); ///< Advance even without input.
    void
    register_module(std::span<const scripting::source_asset> sources,
                    scripting::callable_module entry); ///< Isolated bounded metadata discovery.
    bool visible = false; ///< Explicit graph/normal-UI workspace switch.

    [[nodiscard]] std::string_view status() const noexcept
    {
        return status_;
    } ///< Last visible operation diagnostic.

    /// @brief Refresh only on explicit apply; each running preview retains its own mount.
    void
    preview_mount_factory(std::function<std::shared_ptr<streaming::catalog_streamer>()> factory)
    {
        preview_mount_ = std::move(factory);
    }

  private:
    void apply_preview(); ///< Build a private graph/catalog snapshot on explicit apply.
    std::function<std::shared_ptr<streaming::catalog_streamer>()>
        preview_mount_;                            ///< Explicit mount snapshot factory.
    bool preview_command(std::string_view action); ///< Explicit apply/restart/cancel/stop/traverse.
    void append_preview(panel_plan& plan); ///< Atlas preview and bounded diagnostics controls.
    using field_writer = std::function<void(std::string)>; ///< One validated inspector assignment.
    void inspect(std::vector<control>& rows,
                 const document_session& ui); ///< Materialize typed rows.
    void field(std::vector<control>& rows, const std::string& key, std::string title,
               const std::string& value,
               field_writer write); ///< Bind an editable scalar/name field.
    /// @brief Add one inspector command row.
    void button(std::vector<control>& rows, std::string title, std::string command);
    /// @brief Add one validated immediate inspector edit.
    void operation(std::vector<control>& rows, std::string title, const std::string& key,
                   field_writer apply);
    void edit_field(std::string_view key); ///< Start a deferred text edit.
    void commit_field(); ///< Parse/validate before mutation; errors keep the original document.
    /// @brief Outgoing ordered AND groups and negation.
    void inspect_conditions(std::vector<control>& rows, const std::string& route);
    /// @brief Bind supported control events to graph/registry-backed actions.
    void inspect_events(std::vector<control>& rows, const document_session& ui);
    bool command(std::string_view action, document_session& ui);    ///< Graph vocabulary.
    [[nodiscard]] std::string allocate_id(std::string_view prefix); ///< Collision-checked identity.
    bool create_command(std::string_view action,
                        document_session& ui); ///< New nodes/regions/edges.
    void choose_asset(); ///< Select an asset compatible with the active destination region.
    bool binding_command(std::string_view action,
                         document_session& ui);   ///< Commit UI binding edits.
    bool choice_command(std::string_view action); ///< Bounded typed route/callable choices.
    bool key_input(std::uint32_t key,
                   bool pressed); ///< Deferred inspector text and held modifiers.
    void inspect_region(std::vector<control>& rows); ///< Region ownership and entry fields.
    void inspect_screen(std::vector<control>& rows,
                        const document_session& ui);     ///< Typed node fields.
    void inspect_transition(std::vector<control>& rows); ///< Outgoing edge and decorator fields.
    /// @brief Leaf context and exact scalar argument controls.
    void inspect_condition_values(std::vector<control>& rows, const std::string& route,
                                  const editor_flow::authored_condition& node,
                                  const std::vector<std::size_t>& path, const std::string& key);
    /// @brief Remove one predicate subtree or the complete outgoing decorator.
    void inspect_condition_remove(std::vector<control>& rows, const std::string& route,
                                  const std::vector<std::size_t>& path, const std::string& key,
                                  const std::string& name);
    editor_flow::persistence& document_; ///< Independent source/checkpoint/history owner.
    editor_flow::workspace tool_;        ///< Persistent-ID editing, selection and view transform.
    editor_flow::canvas canvas_;         ///< Shared Atlas input and painting.
    diagnostics::services sinks_;        ///< Optional logs/profiling.
    asset_repository* repository_;       ///< Current immutable typed catalog snapshot.
    scripting::domain registry_;         ///< Discovery has no application mutation capabilities.
    std::vector<scripting::callable_descriptor> descriptors_; ///< Cached generation metadata.
    std::map<std::string, std::pair<std::string, field_writer>>
        fields_;                      ///< Current inspector rows.
    std::string editing_;             ///< Active field key, empty outside text entry.
    std::string text_;                ///< Uncommitted bounded text.
    std::string status_;              ///< Visible validation/storage error or successful operation.
    std::string region_ = "viewport"; ///< Destination region for node creation.
    std::optional<assets::asset_ref> asset_; ///< Compatible selected destination asset.
    std::string route_;     ///< Selected outgoing edge, independent of endpoint ambiguity.
    std::string predicate_; ///< Selected condition descriptor.
    std::string
        identity_scope_;     ///< Fresh authoring namespace prevents deleted-ID reuse after reopen.
    std::uint64_t next_ = 1; ///< Generated suffix, collision checked against existing IDs.
    std::size_t page_ = 0;   ///< Bounded inspector page.
    ui::layout_rect graph_bounds_{};              ///< Current clipped canvas viewport.
    application::ui_input_batch bridge_{nullptr}; ///< Reused portable physical-input adapter.
    bool extend_ = false;                         ///< Held multiselect modifier (M).
    bool pan_ = false;                            ///< Held explicit pan modifier (Space).
    std::optional<std::string> region_selection_; ///< Region inspector selection.
    std::shared_ptr<streaming::catalog_streamer>
        preview_storage_;                                       ///< Retained exact catalog mount.
    std::shared_ptr<std::pmr::memory_resource> preview_memory_; ///< Shared project allocator.
    std::unique_ptr<editor_flow::preview> preview_;             ///< Independent running snapshot.
    std::vector<scripting::source_asset>
        preview_sources_; ///< Latest explicitly discovered sources.
    std::vector<scripting::callable_module> preview_modules_; ///< Frozen on apply, not per frame.
    std::vector<ui::paint_quad> preview_quads_;               ///< Last immutable CPU presentation.
    ui::layout_rect preview_bounds_{}; ///< Actual clipped logical panel area.
    bool preview_visible_ = false;     ///< Explicit graph versus isolated runtime view.
    std::chrono::steady_clock::time_point preview_tick_{}; ///< Host-owned monotonic frame clock.
    std::size_t preview_route_ = 0;                        ///< Bounded diagnostic route selector.
    ui_content::event_action binding_{}; ///< Uncommitted UI event/action inspector state.
};
} // namespace atlas::editor
