/** @file @brief Private snapshot preflight and cached generation-bound runtime event bridges. */
#pragma once
#include <atlas/editor_flow/preview.hpp>
#include <atlas/ui_content/flow_conditions.hpp>
#include <map>
#include <set>
#include <utility>

namespace atlas::editor_flow
{
/// @brief One candidate owns its registration snapshot through asynchronous screen loading.
class preview::impl
{
  public:
    impl(const jobs::job_dispatcher& jobs, std::shared_ptr<std::pmr::memory_resource> memory,
         diagnostics::services sinks); ///< Reuse shared engines and allocator.

    /// @brief Runtime-bound registrations, separate from editor discovery and mutable drafts.
    struct runtime
    {
        runtime_candidate candidate;       ///< Immutable graph and outgoing predicate declarations.
        ui_content::flow_loader loader;    ///< Streamer-backed owner load factory.
        preview_registration registration; ///< Frozen callable source/native snapshot.
        std::unique_ptr<ui_content::flow_manager> flow;      ///< Published only after preflight.
        std::unique_ptr<ui_content::flow_viewport> viewport; ///< Shared real input composition.
        std::map<ui_content::instance*, ui_content::flow_guard_evaluator>
            guards; ///< Exact domains.
        std::map<std::string, ui_content::instance*, std::less<>>
            live; ///< Last closed presentation; guards cannot reenter the manager.
        std::set<std::string, std::less<>> eligible;        ///< Published input eligibility.
        std::optional<ui_content::event_action> navigation; ///< Deferred callback demand.
        std::map<ui_content::instance*,
                 std::map<std::pair<std::string, std::string>, scripting::callable_binding>>
            actions;                                         ///< Cached exact callable generations.
        std::vector<std::pair<std::string, bool>> evaluated; ///< Last 64 guard results.
        bool suppress_actions = false; ///< Retired lifecycle cannot queue work for a replacement.
        std::string route;             ///< Last requested route, including semantic event actions.
        diagnostics::services sinks;   ///< Optional logging/profiling.
        void prepare(const std::string& screen,
                     ui_content::instance& view); ///< Bind once per load.
        void dispatch(const std::string& screen, ui_content::instance& view,
                      const ui_content::event_action& action); ///< Exact owner-domain dispatch.
        bool guard(const ui_content::flow_transition& edge);   ///< Resolve active source domain.
        ui_content::flow_loader factory(); ///< Wrap streamed loads with private bridge preflight.
        void prepare_actions(const std::string& screen,
                             ui_content::instance& view); ///< Typed action preflight.
    };

    void poll(ui::layout_rect bounds); ///< Preflight screens and initial owner lifecycle privately.
    void close(std::shared_ptr<runtime>& state); ///< Cancel and retire before releasing domains.
    void step();    ///< Advance runtime navigation and synchronize its live instance indexes.
    void abandon(); ///< Cancel private loads/navigation without retiring the active presentation.
    ui::layout_engine layout;        ///< Existing threshold-based job-aware layout service.
    ui::paint_engine paint;          ///< Existing opt-in paint strategy; cheap work remains serial.
    diagnostics::services sinks;     ///< Shared application destinations.
    std::shared_ptr<runtime> active; ///< Last complete running snapshot.
    std::shared_ptr<runtime> pending; ///< Replacement never mutates active state before admission.
    std::unique_ptr<ui_content::flow_load> probe; ///< At most one validation load in flight.
    std::size_t cursor = 0;                       ///< Next graph screen to preflight.
    std::uint64_t clock = 1; ///< Increasing owner sync/input/lifecycle identity.
    std::string diagnostic;  ///< Visible failure/status without throwing through the editor loop.
    ui_content::flow_status status = ui_content::flow_status::idle; ///< Last sync result.
    std::thread::id owner = std::this_thread::get_id();             ///< Coordinator affinity.
    bool busy = false;  ///< Reject callback reentry into publication or owner lifecycle.
    void check() const; ///< Reject cross-thread calls before touching Lua or UI state.
};
} // namespace atlas::editor_flow
