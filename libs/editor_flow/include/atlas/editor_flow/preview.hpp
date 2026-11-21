/**
 * @file
 * @brief Isolated, explicitly applied Flow previews with real input replay and diagnostics.
 */
#pragma once
#include <atlas/editor_flow/inspector.hpp>
#include <atlas/editor_flow/session.hpp>
#include <atlas/ui_content/flow_viewport.hpp>

namespace atlas::editor_flow
{
/// @brief Configure callable implementations from immutable native/Lua registration snapshots.
using preview_registration = std::function<void(scripting::domain&)>;

/// @brief Owned diagnostics do not expose runtime pointers across an editor frame.
struct preview_screen
{
    std::string region; ///< Active region identity.
    std::string screen; ///< Active graph instance.
    std::string focus;  ///< Current authored focus, empty when another layer owns keyboard input.
    bool input = false; ///< Pointer/navigation eligibility, not simultaneous keyboard focus.
    bool popup = false; ///< Explicit or legacy overlay presentation.
};

/// @brief Structural paths do not prove arbitrary Lua predicates or authored input reachability.
struct traversal_plan
{
    std::vector<std::string> routes;      ///< Bounded forward graph route identities.
    std::vector<std::string> conditioned; ///< Routes requiring real domain/context evaluation.
};

/// @brief Own one independent preview; services and loaders must outlive accepted reads.
class preview
{
  public:
    explicit preview(const jobs::job_dispatcher& jobs,
                     std::shared_ptr<std::pmr::memory_resource> memory = {},
                     diagnostics::services sinks = {}); ///< Reuse the application's job pool.
    ~preview(); ///< Cancel candidate reads and shut down owner lifecycle before releasing state.
    preview(const preview&) = delete;            ///< Mutable input and Lua domains have one owner.
    preview& operator=(const preview&) = delete; ///< Never alias preview ownership.
    /// @brief Preflight all screens privately; failure preserves the last running preview.
    void apply(runtime_candidate candidate, ui_content::flow_loader loader,
               preview_registration registrations = {});
    void cancel();  ///< Drop only an unapplied replacement and pending runtime navigation.
    void restart(); ///< Reconstruct the last applied snapshot, independent of current edits.
    void stop();    ///< Cancel and retire active views; no source document is modified.
    /// @brief Poll, synchronize and process real/replayed input through the shared viewport.
    [[nodiscard]] std::vector<ui::paint_quad> frame(ui::layout_rect bounds,
                                                    std::span<const ui::input_event> events = {},
                                                    ui_content::flow_frame_options options = {});
    [[nodiscard]] bool request(std::string_view route); ///< Explicit diagnostic traversal action.
    [[nodiscard]] bool running() const;                 ///< A complete preview has been installed.
    [[nodiscard]] bool applying() const; ///< A private replacement is still being validated.
    [[nodiscard]] std::string_view diagnostic() const;    ///< Last apply/runtime error or status.
    [[nodiscard]] ui_content::flow_status status() const; ///< Last runtime synchronization outcome.
    [[nodiscard]] std::string_view active_route() const;  ///< Last explicitly requested route.
    [[nodiscard]] std::vector<preview_screen> presentation() const; ///< Owned active-layer summary.
    /// @brief Copy lightweight region histories without retaining retired views.
    [[nodiscard]] std::vector<std::pair<std::string, std::vector<std::string>>> history() const;
    [[nodiscard]] std::vector<std::pair<std::string, bool>>
    conditions() const; ///< Bounded results.
    /// @brief Plan a structural path and identify routes requiring actual predicate evaluation.
    [[nodiscard]] traversal_plan plan(std::string_view from, std::string_view to) const;
    [[nodiscard]] std::vector<std::string> routes() const; ///< Applied diagnostic route choices.
  private:
    class impl; ///< Independent candidate, registry bindings and UI phase ownership.
    std::unique_ptr<impl> impl_; ///< All native platform concerns remain outside this library.
};
} // namespace atlas::editor_flow
