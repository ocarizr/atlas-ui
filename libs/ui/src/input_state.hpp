/** @file @brief Private bounded interaction ownership and synchronous dispatch helpers. */
#pragma once
#include "paint_state.hpp"
#include <algorithm>
#include <atlas/jobs/execution.hpp>
#include <atlas/ui/input_session.hpp>

namespace atlas::ui
{
struct input_binding
{
    element_handle target;                  ///< Retained identity, never a registry pointer.
    std::shared_ptr<input_handler> handler; ///< Lives through disable/end after removal.
    interaction_options options;            ///< Applied only between frames.
    bool started = false;                   ///< Start has been attempted once.
    bool enabled = false;                   ///< Current active lifecycle state.
    bool retiring = false;                  ///< Explicit unbind waits for serial cleanup.
    bool ended = false;                     ///< End has been attempted once.
}; ///< Stable slot/generation order defines system broadcast order.

/// @brief Private serial state independent of applications.
class input_session::impl
{
  public:
    /// @brief Reserve bounded bindings without acquiring another scheduler.
    impl(element_tree& tree, input_limits limits, std::shared_ptr<std::pmr::memory_resource> memory,
         diagnostics::services destinations);
    ///< Reserve bounded bindings without acquiring another scheduler.
    void check() const;   ///< Owner check precedes mutable observation.
    void idle() const;    ///< Configuration only between frames or before the first frame.
    void open() const;    ///< Require an admitted successful nonterminal frame.
    void fail() noexcept; ///< Preserve original failure and discard only unsealed commands.
    input_binding* find(element_handle target);             ///< Exact generation/session identity.
    const input_binding* find(element_handle target) const; ///< Const identity lookup.
    /// @brief Resolve only identities captured by this frame.
    const detail::presentation_region* region(element_handle target) const;
    ///< Resolve only identities captured by this frame.
    bool
    eligible(element_handle target) const; ///< Live visible/enabled ancestry and captured links.
    element_handle hit(layout_point point) const; ///< Front-to-back registered border/clip lookup.
    /// @brief Bounded callback delivery with copied values and retained handler ownership.
    event_response send(element_handle current, event_kind kind, route_phase phase,
                        element_handle target, input_event raw = {});
    ///< Bounded callback with copied values; cleanup may reference a removed element.
    void reconcile();                ///< Cancel stale focus/capture/hover and lifecycle states.
    void broadcast(event_kind kind); ///< Stable serial active registrations.
    void set_focus(element_handle target); ///< Direct transition plus ancestor reveal requests.
    void clear_capture();                  ///< Reset before delivering capture-lost.
    void set_hover(element_handle target,
                   input_event raw = {}); ///< Direct leave/enter transitions.
    void route(input_event raw, element_handle target, event_kind kind); ///< Capture/target/bubble.
    /// @brief Validate response transitions before changing ownership.
    void respond(input_binding& binding, const event_response& response, input_event raw);
    ///< Validate requested state transitions before applying them.
    void process_one(input_event raw); ///< Semantic packet to routing/state changes.
    /// @brief Shared guarded tick, late-tick and end transition.
    void system_stage(event_kind kind);

    ///< Shared guarded tick/late/end transition.
    /// @brief Restore the owner guard on every exit.
    struct guard
    {
        bool& busy; ///< Sole owner flag.

        explicit guard(bool& value) : busy(value)
        {
            busy = true;
        } ///< Enter checked operation.

        ~guard()
        {
            busy = false;
        } ///< Always leave, including callback exceptions.
    };

    element_tree& tree;                                ///< Borrowed; must outlive this facade.
    input_limits limits;                               ///< Independent metadata/callback bounds.
    std::shared_ptr<std::pmr::memory_resource> memory; ///< Outlives PMR vectors.
    diagnostics::services destinations;                ///< Optional logging/profiling.
    std::pmr::vector<input_binding> bindings; ///< Sorted by slot/generation, no per-widget jobs.
    std::pmr::vector<detail::presentation_region> regions; ///< Immutable frozen frame geometry.
    std::pmr::vector<std::size_t> slots;   ///< Slot to region lookup; no live registry access.
    std::pmr::vector<element_handle> path; ///< Reserved route scratch, bounded by ancestry policy.
    element_handle focus_target{}, capture_target{}, hover_target{}; ///< Separate ownership states.
    input_statistics stats; ///< Current/last admitted frame, not process memory.
    float delta = 0;        ///< Finite frame duration.
    unsigned stage = 0;     ///< 0 closed, 1 input, 2 tick, 3 late, 4 end, 5 failed.
    bool terminal = false;  ///< Explicit shutdown permanently stops admission.
    bool busy = false;      ///< Reentrant callback/diagnostic calls are rejected.
    std::uint64_t session;  ///< Bound tree identity.
    const std::thread::id owner = std::this_thread::get_id(); ///< Sole serial event owner.
};
} // namespace atlas::ui
