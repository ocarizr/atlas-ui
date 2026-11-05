/**
 * @file
 * @brief Owner-thread input routing and ordered lifecycle over immutable presentation geometry.
 */
#pragma once
#include <atlas/ui/element_tree.hpp>

namespace atlas::ui
{
/// @brief Platform adapters and replay supply logical viewport coordinates and semantic key IDs.
enum class input_kind : std::uint8_t
{
    pointer_move,   ///< Primary-pointer movement.
    pointer_down,   ///< Primary-button press.
    pointer_up,     ///< Primary-button release; capture ends after routing.
    pointer_cancel, ///< Cancel capture without clearing keyboard focus.
    pointer_leave,  ///< Pointer left the viewport; capture can continue.
    key_down,       ///< Semantic key press, routed to focus.
    key_up,         ///< Semantic key release, routed to focus.
    window_blur     ///< Release capture, hover and focus after activation/input loss.
};

struct input_event
{
    input_kind kind = input_kind::pointer_move; ///< Validated before any batch callbacks.
    layout_point position{}; ///< Logical viewport coordinates; may lie outside the viewport.
    std::uint32_t key = 0;   ///< Host-defined semantic identifier, not a native SDK type.
}; ///< Value-only, replayable primary-pointer/key packet; no platform or device ownership.

/// @brief Direct lifecycle events and routed input share one serial handler interface.
enum class event_kind : std::uint8_t
{
    start,          ///< First active participation, attempted once.
    enable,         ///< Transition to effective enabled state.
    disable,        ///< Transition away from effective enabled state.
    end,            ///< Start once, enable/disable transitions, end once.
    process_input,  ///< Before raw packets in the input stage.
    tick,           ///< Normal serial update production.
    late_tick,      ///< Serial production after the tick work boundary.
    end_frame,      ///< Ordered system stages.
    pointer_move,   ///< Primary-pointer motion routed through hierarchy.
    pointer_down,   ///< Primary-button press routed through hierarchy.
    pointer_up,     ///< Primary-button release routed through hierarchy.
    pointer_cancel, ///< Primary-pointer routing.
    pointer_enter,  ///< Direct geometric hover entry.
    pointer_leave,  ///< Direct geometric hover exit.
    capture_lost,   ///< Direct state notifications.
    key_down,       ///< Press routed to current focus.
    key_up,         ///< Release routed to current focus.
    focus_gained,   ///< Direct focus acquisition.
    focus_lost,     ///< Keyboard routing and focus transitions.
    ensure_visible  ///< Bubble a focus-reveal request; scrolling policy belongs to the container.
};

/// @brief Direct notifications and the three ordered hierarchy routing phases.
enum class route_phase : std::uint8_t
{
    direct,  ///< Lifecycle and state notifications.
    capture, ///< Root toward target's parent.
    target,  ///< Original destination.
    bubble   ///< Parent toward root, or an ancestor focus-reveal request.
}; ///< Painter order determines the hit; hierarchy determines its route.

struct routed_event
{
    event_kind kind;            ///< Semantic notification.
    route_phase phase;          ///< Delivery stage.
    element_handle target;      ///< Original destination, including generation/session.
    element_handle current;     ///< Handler receiving this delivery.
    layout_point position;      ///< Logical viewport coordinates.
    layout_point local;         ///< Position relative to the current translated border.
    layout_rect target_bounds;  ///< Frozen translated target border for focus reveal.
    layout_rect current_bounds; ///< Frozen translated receiving element border.
    layout_rect clip;           ///< Receiving element's intersected ancestor clip.
    std::uint32_t key = 0;      ///< Semantic key identifier from the input packet.
    std::uint64_t frame = 0;    ///< Increasing owner-selected frame identity.
    float delta_seconds = 0;    ///< Finite nonnegative frame delta.
    bool target_alive = false;  ///< Cleanup can refer to an already removed identity.
}; ///< Borrowed only for callback duration; copying this value is safe.

struct event_response
{
    bool stop = false;            ///< Stop the current route after this handler.
    bool capture_pointer = false; ///< Capture to current on pointer-down only.
    bool release_pointer = false; ///< Release current capture from a pointer route.
    bool focus = false;           ///< Focus current if configured focusable.
    bool blur = false;            ///< Clear keyboard focus.
}; ///< Direct notifications ignore responses; ensure-visible honors only stop.

/// @brief Retained adapter for native behavior and future Lua dispatch.
class input_handler
{
  public:
    virtual ~input_handler() = default; ///< Retained through callback/cleanup delivery.
    /// @brief Produce bounded commands; heavy work belongs to later joined phases.
    virtual event_response on_event(const routed_event& event, command_buffer& commands) = 0;
    ///< Produce bounded commands on the owner; expensive work belongs to later joined phases.
};

struct interaction_options
{
    bool enabled = true;    ///< A disabled registration also disables its descendants.
    bool hit_test = true;   ///< Own border participates even without a painted background.
    bool focusable = false; ///< Explicit programmatic/response focus eligibility.
}; ///< Sparse registration policy, independent of geometry and widget type.

struct input_limits
{
    std::size_t max_nodes = 65536;     ///< Captured presentation bound, at most 65536.
    std::size_t max_bindings = 1024;   ///< Registered handlers including pending retirement.
    std::size_t max_events = 256;      ///< Validated input packets per frame, at most 4096.
    std::size_t max_route_depth = 64;  ///< Maximum registered target ancestry, at most 65536.
    std::size_t max_callbacks = 65536; ///< Includes lifecycle/route/state deliveries per frame.
}; ///< Metadata and command/scheduler allocations have separate accounting.

/// @brief Last admitted frame and callback attempts, excluding arbitrary handler allocations.
struct input_statistics
{
    std::uint64_t frame = 0;   ///< Last admitted frame.
    std::size_t events = 0;    ///< Input packets consumed in order.
    std::size_t callbacks = 0; ///< Callback attempts, including one that throws.
    bool closed = true;        ///< End-frame or shutdown completed.
    bool failed = false;       ///< Further dispatch waits for a newer begin-frame or shutdown.
};

class input_session
{
  public:
    explicit input_session(element_tree& tree, input_limits limits = {},
                           std::shared_ptr<std::pmr::memory_resource> memory = {},
                           diagnostics::services destinations = {}); ///< Borrow a live owner tree.
    ~input_session(); ///< Owner-only, no implicit callbacks; explicit shutdown delivers cleanup.
    input_session(const input_session&) = delete;            ///< One serial interaction owner.
    input_session& operator=(const input_session&) = delete; ///< Never alias mutable state.
    void bind(element_handle target, std::shared_ptr<input_handler> handler,
              interaction_options options = {}); ///< Between frames; retain handler ownership.
    void configure(element_handle target, interaction_options options); ///< Between frames only.
    void unbind(element_handle target); ///< Retire at next begin-frame, with disable/end cleanup.
    /// @brief Prepare matching geometry, begin commands, then start/enable registered handlers.
    void begin_frame(std::uint64_t frame, const element_snapshot& source,
                     const layout_snapshot& geometry, float delta_seconds = 0);
    ///< Prepare matching geometry, begin the tree's command frame, then start/enable handlers.
    void process(std::span<const input_event> events); ///< Exactly once, before tick.
    void tick();      ///< Broadcast after input; callers can join work between serial stages.
    void late_tick(); ///< Broadcast after tick and any caller-owned sync point.
    void end_frame(); ///< Broadcast after late tick; caller seals/applies remaining commands.
    void focus(element_handle target); ///< During an open frame; empty clears focus.
    element_handle focused() const;    ///< Owner-only observation outside callbacks.
    element_handle captured() const;   ///< Primary-pointer capture, or empty.
    element_handle hovered() const;    ///< Geometric hover, independent of capture.
    element_handle hit_test(layout_point point) const; ///< Half-open bounds and ancestor clips.
    void shutdown(std::uint64_t frame);  ///< New terminal command frame; disable/end exactly once.
    input_statistics statistics() const; ///< Serial owner observation.
  private:
    class impl; ///< Bounded handlers, immutable presentation and owner lifecycle state.
    std::unique_ptr<impl> impl_; ///< Tree and services must outlive synchronous calls.
}; ///< No private pool, blocking worker or callbacks from jobs into mutable session state.
} // namespace atlas::ui
