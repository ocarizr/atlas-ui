/**
 * @file
 * @brief Owner-affine typed state, bounded queued actions/events and observable changes. */
#pragma once
#include <atlas/mvvm/schema.hpp>

namespace atlas::mvvm
{
/// @brief Notifications use independent action/event namespaces.
enum class notification_kind : std::uint8_t
{
    changed, ///< Distinct committed field value.
    action,  ///< Invoked action, including repeated identical invocations.
    event    ///< Explicit emitted event, including repeated identical emissions.
};

/// @brief Borrowed only during one synchronous owner callback.
struct notification
{
    notification_kind kind;           ///< Address namespace.
    std::uint32_t index;              ///< Definition-order slot in that namespace.
    std::span<const value> arguments; ///< One committed new value for a field, ordered signal args.
    std::uint64_t frame;              ///< Current increasing frame/transaction identity.
};
class view_model; ///< Unique serial mutation owner.

/// @brief Atomic command batches keep relative updates distinct from absolute assignments.
enum class update_kind : std::uint8_t
{
    assign,      ///< Absolute field value or ordered action/event invocation.
    add_integer, ///< Checked integer delta.
    add_number   ///< Checked finite numeric delta.
};

/// @brief Owned typed command for transactional producer admission, never immediate publication.
struct command
{
    std::variant<field_handle, action_handle, event_handle> target; ///< Validated schema slot.
    update_kind update = update_kind::assign; ///< Relative updates require a numeric field.
    std::vector<value> arguments;             ///< One field value or exact signal protocol.
};

/// @brief Retained observer; heavy work belongs to later joined phases.
class observer
{
  public:
    virtual ~observer() = default; ///< Release on the owner after subscription cleanup.
    /// @brief Read committed state and enqueue subsequent bounded demands.
    virtual void on_notify(const notification& event, view_model& model) = 0;
    ///< Read committed values and enqueue demands without recursively flushing.
};

/// @brief Subscription token is instance-bound and never reused.
struct subscription
{
    std::uint64_t instance = 0; ///< Model owner identity.
    std::uint64_t sequence = 0; ///< Nonzero monotonically assigned token.
};

/// @brief Frame-wide budgets include callback-produced and coalesced demands.
struct runtime_limits
{
    std::size_t max_commands = 256;         ///< Total admitted demands per frame, at most 65536.
    std::size_t max_arguments = 1024;       ///< Retained queue values, at most 1048576.
    std::size_t max_payload_bytes = 262144; ///< Text bytes admitted per frame, at most 16777216.
    std::size_t max_subscriptions = 256;    ///< At most 65536 retained observers.
    std::size_t max_callbacks = 4096;       ///< At most 1048576 attempts; bounds feedback cycles.
    std::size_t max_warning_details = 4;    ///< Coalescing conflict diagnostics per frame.
};

/// @brief Last frame counters; equality suppression is distinct from explicit signal delivery.
struct statistics
{
    std::uint64_t frame = 0;   ///< Last admitted frame.
    std::size_t commands = 0;  ///< Includes writes that coalesced or produced no change.
    std::size_t callbacks = 0; ///< Attempts, including a failing observer.
    std::size_t conflicts = 0; ///< Different pending values coalesced for one field.
    bool failed = false;       ///< Explicit reset required after an admitted flush failure.
};

/// @brief Reusable ViewModel instance; no UI, scripting VM, scheduler or storage ownership.
class view_model
{
  public:
    /// @brief Allocate typed instance columns and reserve transport metadata.
    explicit view_model(const schema& description, runtime_limits limits = {},
                        std::shared_ptr<std::pmr::memory_resource> memory = {},
                        const diagnostics::services& destinations = {});
    ///< Allocate typed storage and reserve bounded command/subscription metadata.
    ~view_model(); ///< Owner-only destruction outside dispatch; no implicit callbacks.
    view_model(const view_model&) = delete;            ///< One mutable owner.
    view_model& operator=(const view_model&) = delete; ///< Share immutable schemas, not mutation.
    const schema& description() const;     ///< Owner observation, including inside callbacks.
    bool boolean(field_handle slot) const; ///< O(1) checked typed read.
    std::int64_t integer(field_handle slot) const;  ///< O(1) checked typed read.
    double number(field_handle slot) const;         ///< O(1) checked typed read.
    std::string_view text(field_handle slot) const; ///< Borrow until the next committed write.
    value read(field_handle slot) const;   ///< Owned generic value; text copies may allocate.
    void begin_frame(std::uint64_t frame); ///< Increasing identity; prior queue must be drained.
    void set(field_handle slot,
             value data); ///< Validate then queue; equal pending writes coalesce.
    void add_integer(field_handle slot,
                     std::int64_t delta);             ///< Queue an ordered checked integer delta.
    void add_number(field_handle slot, double delta); ///< Queue an ordered finite numeric delta.
    void invoke(action_handle slot, std::span<const value> arguments = {}); ///< Queue an action.
    void emit(event_handle slot,
              std::span<const value> arguments = {}); ///< Queue an explicit event.
    subscription subscribe(field_handle slot,
                           std::shared_ptr<observer> listener); ///< Between flushes.
    subscription subscribe(action_handle slot,
                           std::shared_ptr<observer> listener); ///< Between flushes.
    subscription subscribe(event_handle slot,
                           std::shared_ptr<observer> listener); ///< Between flushes.
    void unsubscribe(subscription token); ///< Idempotent; safe from the current callback.
    void flush(); ///< Iterative owner dispatch; callback demands append to the same bounded queue.
    void submit(std::span<const command> commands); ///< Admit all demands or preserve prior queue.
    void reset(); ///< Discard pending demands/failure; preserve already committed values.
    statistics inspect() const; ///< Current owner statistics.
  private:
    class impl;                  ///< Reserved typed arrays, queue and observer registrations.
    std::unique_ptr<impl> impl_; ///< Retains schema, allocator and optional diagnostics.
};
} // namespace atlas::mvvm
