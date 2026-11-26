/** @file @brief Private owner-affine gesture state and checked weak view leases. */
#pragma once
#include "input_state.hpp"
#include <atlas/input/key.hpp>

namespace atlas::ui
{
/// @brief One gesture spans frames; frame leases never keep an input owner alive.
class drag_drop_session::impl
{
  public:
    struct endpoint
    {
        std::weak_ptr<input_lifetime> lifetime; ///< Invalidated before input destruction.
        element_handle target{};                ///< Full generation identity.
        std::shared_ptr<input_handler> handler; ///< Retained terminal notification recipient.
    }; ///< No registry or transient element pointer is retained.

    impl(drag_limits policy, std::shared_ptr<std::pmr::memory_resource> resource,
         diagnostics::services services);       ///< Validate before reserving bounded frame leases.
    void check(bool require_open = true) const; ///< Owner, non-reentry and frame admission.
    input_session::impl* view(const endpoint& value) const; ///< Admitted live view only.
    endpoint capture(input_session& input,
                     element_handle target) const; ///< Freeze handler identity.
    bool valid_source() const;               ///< Require unchanged offer policy and owned capture.
    bool valid(const endpoint& value) const; ///< Same eligible element and registration.
    const drag_source_options* source_options(const endpoint& value) const; ///< Sparse lookup.
    const drop_target_options* target_options(const endpoint& value) const; ///< Sparse lookup.
    event_response
    emit(const endpoint& value, event_kind kind,
         drag_cancel_reason reason = drag_cancel_reason::none); ///< Bounded callback.
    void clear_capture();                   ///< Only release this gesture's source capture.
    void reset() noexcept;                  ///< Release retained gesture values, not user data.
    void cancel(drag_cancel_reason reason); ///< Best-effort terminal delivery with first failure.
    void failure() noexcept; ///< Preserve original exception while cancelling uncommitted state.
    void visit();            ///< Charge frame work before visiting a candidate.
    bool start(endpoint value, drag_operation operation, bool keyboard); ///< Prepare/capture.
    drag_operation negotiate(const endpoint& candidate); ///< Static policy then pure user query.
    void select(endpoint candidate, drag_operation operation); ///< Ordered leave/enter/over.
    void point(layout_point position); ///< Deepest typed ancestor of frontmost hit.
    void next(bool previous);          ///< Stable presented-order traversal across views.
    void request();                    ///< Allocate a ticket; never mutate application data.
    bool
    preflight(std::uint64_t ticket) const; ///< Structural checks without callback side effects.
    void arm(layout_point position);       ///< Find an eligible source under the frontmost hit.
    bool process(input_event event);       ///< Shared pointer/semantic state machine.
    bool keyboard(input_event event);      ///< Portable keys and semantic actions.
    void release_views() noexcept;         ///< End leases even if an input owner was destroyed.

    template <class Function> decltype(auto) run(Function&& body)
    {
        check();
        const input_session::impl::guard lock{busy};
        const diagnostics::operation_scope scope{sinks, {"ui.drag.dispatch", "ui.dispatch"}};
        try
        {
            return body();
        }
        catch (...)
        {
            failure();
            throw;
        }
    } ///< Callback failures leave no stale capture, pending transfer or borrowed payload.

    drag_limits limits;                                    ///< Independent bounded work.
    std::shared_ptr<std::pmr::memory_resource> memory;     ///< Outlives PMR leases.
    diagnostics::services sinks;                           ///< Optional trace/log services.
    std::pmr::vector<std::weak_ptr<input_lifetime>> views; ///< Back-to-front frame order.
    endpoint source{}, target{};                     ///< Independently owned participating panels.
    drag_source_options offered{};                   ///< Policy frozen on admission.
    drop_target_options accepted{};                  ///< Policy frozen on pending request.
    drag_payload payload{};                          ///< Shared immutable values.
    drag_feedback feedback{};                        ///< Cheap rendering observation.
    drag_completion completion{};                    ///< No payload retention after terminal state.
    drag_actions actions{};                          ///< Semantic device-independent alternatives.
    layout_point origin{};                           ///< Logical threshold origin.
    drag_operation preferred = drag_operation::none; ///< Explicit preference never silently flips.
    std::uint64_t frame = 0, serial = 0, ticket = 0; ///< Nonreused frame and transfer identities.
    std::size_t queries = 0, packets = 0;            ///< Reset on each domain frame.
    bool open = false, busy = false, keys = true, shift = false; ///< Serial admission state.
    const std::thread::id owner = std::this_thread::get_id();    ///< Same owner as every view.
};
} // namespace atlas::ui
