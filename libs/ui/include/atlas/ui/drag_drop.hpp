/**
 * @file
 * @brief Window-scoped internal drag-and-drop with owned payloads and explicit transfer
 * confirmation.
 */
#pragma once
#include <atlas/ui/layout_types.hpp>
#include <optional>
#include <string>
#include <vector>

namespace atlas::ui
{
class input_session; ///< Mutable input owner participating in a window drag domain.
struct input_event;  ///< Replayable logical input packet.
/// @brief Transfer choice; none is also the rejected visual state.
enum class drag_operation : std::uint8_t
{
    none, ///< No accepted transfer.
    copy, ///< Preserve source data.
    move  ///< Application transaction transfers ownership before confirmation.
};
/// @brief Source/target capabilities; no filesystem copy/move is implied.
enum class drag_operations : std::uint8_t
{
    copy = 1,     ///< Copy only.
    move = 2,     ///< Move only.
    copy_move = 3 ///< Negotiate either operation.
};
/// @brief Dynamic target response; defer uses the declared typed policy.
enum class drag_acceptance : std::uint8_t
{
    defer,  ///< Use static accepted types and operations.
    accept, ///< Accept the negotiated choice.
    reject  ///< Reject this target and try an eligible ancestor.
};

/// @brief Shared immutable payload; caller buffers are copied once, not borrowed by a gesture.
class drag_payload
{
  public:
    drag_payload() = default; ///< No offered data.
    static drag_payload
    binary(std::string_view type, std::span<const std::byte> bytes,
           std::shared_ptr<std::pmr::memory_resource> memory = {}); ///< Bounded arbitrary bytes.
    static drag_payload
    text(std::string_view type, std::string_view text,
         std::shared_ptr<std::pmr::memory_resource> memory = {}); ///< Bounded UTF-8 text.
    explicit operator bool() const noexcept; ///< Whether a typed payload exists.
    std::string_view type() const noexcept;  ///< Stable type key, not a path or registry pointer.
    std::span<const std::byte> bytes() const noexcept; ///< Owned values while this handle survives.
    std::string_view text() const noexcept;            ///< Empty for a binary payload.
    bool is_text() const noexcept;                     ///< Distinguish empty text from binary data.
    bool operator==(const drag_payload&) const = default; ///< Compare immutable offer identity.
  private:
    class data;                        ///< Retained allocator plus immutable type/bytes.
    std::shared_ptr<const data> data_; ///< Shared lifetime, no mutable public alias.
};

/// @brief Quiescent source configuration; absent payload lets a prepare handler supply one.
struct drag_source_options
{
    drag_payload payload{}; ///< Reused immutable offer, or dynamically supplied data.
    drag_operations operations = drag_operations::copy; ///< Source capabilities.
    float threshold = 4; ///< Logical-unit pointer travel before capture changes ownership.
};

/// @brief Explicit typed drop policy; missing configuration never accepts a drop.
struct drop_target_options
{
    std::vector<std::string> types;                     ///< Exact type keys, at most 32.
    drag_operations operations = drag_operations::copy; ///< Accepted transfer capabilities.
};
/// @brief Terminal reason; an uncommitted cancellation never removes application source data.
enum class drag_cancel_reason : std::uint8_t
{
    none,               ///< Successful transfer.
    rejected,           ///< No accepting target or application rejected the transaction.
    explicit_cancel,    ///< Caller or cancel action requested cancellation.
    window_lost,        ///< Native focus/capture cancellation.
    source_unavailable, ///< Source identity, visibility or policy changed.
    target_unavailable, ///< Pending destination identity or policy changed.
    context_lost,       ///< Participating input owner was retired or omitted.
    callback_failure    ///< A nonterminal handler failed before commitment.
};

/// @brief Value-only metadata delivered with drag lifecycle events.
struct drag_event
{
    element_handle source;                           ///< Full source identity.
    element_handle target;                           ///< Current negotiated destination, or empty.
    drag_payload payload;                            ///< Retained immutable values.
    drag_operation operation = drag_operation::none; ///< Negotiated choice.
    drag_cancel_reason reason = drag_cancel_reason::none; ///< Terminal cancellation detail.
    std::uint64_t transfer = 0; ///< Nonreused confirmation ticket when a drop is pending.
    std::uint64_t frame = 0;    ///< Window drag-domain frame, independent of local view counters.
};

/// @brief An application transaction receives this data after event dispatch, before confirmation.
struct drop_request
{
    std::uint64_t transfer = 0; ///< Exact ticket required by resolve.
    element_handle source;      ///< Stable identity at admission.
    element_handle target;      ///< Destination identity at admission.
    drag_payload payload;       ///< Keeps values alive through asynchronous preparation.
    drag_operation operation = drag_operation::none; ///< Never silently changed on confirmation.
};
/// @brief Gesture phase; only the explicit pending state can await asynchronous application work.
enum class drag_phase : std::uint8_t
{
    idle,     ///< No retained gesture.
    armed,    ///< A press has not crossed its source threshold.
    dragging, ///< Pointer or keyboard navigation is selecting a destination.
    pending   ///< Application owns a requested transaction, awaiting resolve.
};

/// @brief Cheap feedback for application painting/cursors; it does not invalidate layout.
struct drag_feedback
{
    drag_phase phase = drag_phase::idle;             ///< Current interaction state.
    layout_point position{};                         ///< Logical window position.
    element_handle source;                           ///< Empty while idle.
    element_handle target;                           ///< Empty if no accepted destination.
    drag_operation operation = drag_operation::none; ///< Copy/move or rejected feedback.
    bool keyboard = false; ///< Lift/target/confirm alternative to pointer dragging.
};

/// @brief Last terminal outcome does not retain application payload data.
struct drag_completion
{
    std::uint64_t transfer = 0; ///< Zero for a gesture cancelled before requesting a transfer.
    bool committed = false;     ///< True only after application confirmation.
    drag_cancel_reason reason = drag_cancel_reason::none; ///< Cancellation or success.
};

/// @brief Device-independent bindings; all zero leaves the optional keyboard controls enabled.
struct drag_actions
{
    std::uint32_t lift = 0;     ///< Lift the focused configured source.
    std::uint32_t confirm = 0;  ///< Request a drop on the current destination.
    std::uint32_t cancel = 0;   ///< Cancel without application mutation.
    std::uint32_t next = 0;     ///< Next configured destination across participating panels.
    std::uint32_t previous = 0; ///< Previous configured destination.
    std::uint32_t copy = 0;     ///< Explicit copy preference.
    std::uint32_t move = 0;     ///< Explicit move preference.
};

/// @brief Shared window metadata bounds, independent of each input owner's callback budget.
struct drag_limits
{
    std::size_t max_views = 64;      ///< Simultaneously admitted input contexts.
    std::size_t max_queries = 65536; ///< Candidate/policy visits per domain frame.
};

/// @brief One owner-thread interaction domain, shared by panels in one native window.
class drag_drop_session
{
  public:
    /// @brief Own one window interaction domain without a private pool or native dependencies.
    explicit drag_drop_session(drag_limits limits = {},
                               std::shared_ptr<std::pmr::memory_resource> memory = {},
                               diagnostics::services sinks = {});
    ///< No scheduler, native SDK or filesystem access.
    ~drag_drop_session(); ///< Release borrowed-frame guards and values; no implicit user callbacks.
    drag_drop_session(const drag_drop_session&) = delete;            ///< One native-window owner.
    drag_drop_session& operator=(const drag_drop_session&) = delete; ///< Do not alias a domain.
    void configure(drag_actions actions = {}, bool keyboard = true); ///< Between domain frames.
    /// @brief Lease matching open input phases in explicit back-to-front window order.
    void begin_frame(std::uint64_t frame, std::span<input_session* const> views);
    ///< All views must have opened and processed their initial input batch; back-to-front order.
    bool
    process(const input_event& event); ///< True consumes drag input; false permits normal routing.
    bool
    lift(input_session& owner, element_handle source,
         drag_operation preferred = drag_operation::none); ///< Explicit keyboard/semantic lift.
    void next_target(bool previous = false); ///< Deterministic cross-panel target selection.
    std::optional<drop_request>
    pending_drop() const; ///< Owned request for the application transaction.
    bool
    can_commit(std::uint64_t transfer) const; ///< Check immediately before the owner transaction.
    void resolve(std::uint64_t transfer,
                 bool committed); ///< Confirm only after atomic domain commit.
    void
    cancel(drag_cancel_reason reason = drag_cancel_reason::explicit_cancel); ///< Open frame only.
    void end_frame(); ///< End borrowed view access before any participant closes its input frame.
    drag_feedback feedback() const;     ///< Owner-only observational feedback.
    drag_completion completion() const; ///< Last committed/cancelled outcome.
  private:
    class impl;                  ///< Bounded view leases, gesture and replay state.
    std::unique_ptr<impl> impl_; ///< Owner-affine storage.
};
} // namespace atlas::ui
