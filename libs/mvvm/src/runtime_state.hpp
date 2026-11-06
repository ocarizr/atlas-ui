/** @file @brief Typed instance columns and bounded serial demand/subscription metadata. */
#pragma once
#include "schema_state.hpp"
#include <atlas/mvvm/view_model.hpp>
#include <limits>
#include <thread>

namespace atlas::mvvm
{
/// @brief Absolute and relative numeric mutations have explicit coalescing semantics.
enum class mutation_kind : std::uint8_t
{
    assign,      ///< Absolute field write, coalescible within a pending segment.
    add_integer, ///< Preserve every signed integer delta.
    add_number   ///< Preserve every finite double delta.
};

/// @brief One owned queue record borrows values from the reserved argument arena.
struct queued_command
{
    notification_kind kind;    ///< Slot namespace.
    std::uint32_t index;       ///< Checked slot index.
    std::size_t offset, count; ///< Reserved argument range.
    mutation_kind mutation =
        mutation_kind::assign; ///< Relative operations are never coalesced as replacements.
};

/// @brief Per-field coalescing index, valid only in one pending write segment.
struct pending_field
{
    std::uint64_t segment = 0; ///< Invalidated at each signal/dispatch boundary.
    std::size_t command = 0;   ///< Pending command index.
};

/// @brief Tokens are never reused even when vacant registration storage is reused.
struct subscription_bucket
{
    std::size_t head = std::numeric_limits<std::size_t>::max(); ///< First registration in authored
                                                                ///< subscription order.
    std::size_t tail = std::numeric_limits<std::size_t>::max(); ///< Last appended registration.
};

/// @brief One linked observer registration; a retired slot remains safe during traversal.
struct registration
{
    notification_kind kind;                                     ///< Slot namespace.
    std::uint32_t index;                                        ///< Checked slot index.
    std::uint64_t token;                                        ///< Instance-local sequence.
    std::shared_ptr<observer> listener;                         ///< Null means unsubscribed.
    std::size_t next = std::numeric_limits<std::size_t>::max(); ///< Next matching registration.
};

/// @brief No UI types or mutable global model state cross this implementation boundary.
class view_model::impl
{
  public:
    impl(const schema& description, runtime_limits policy,
         std::shared_ptr<std::pmr::memory_resource> resource,
         diagnostics::services services); ///< Allocate columns and reserve bounded queues.
    void owner() const;                   ///< Check immutable thread identity before mutable state.
    void writable() const;                ///< Require an admitted healthy frame.
    std::uint32_t offset(field_handle slot,
                         value_type type) const;                ///< Checked typed column offset.
    value read(field_handle slot) const;                        ///< Generic owned boundary value.
    bool matches(std::uint32_t field, const value& data) const; ///< Nonallocating typed equality.
    std::size_t bucket(notification_kind kind,
                       std::uint32_t index) const; ///< Direct namespace bucket index.
    void unlink(std::size_t index); ///< Remove a retired slot before reuse at a boundary.
    void resolve(const queued_command& command,
                 value& argument) const;                 ///< Compute checked relative candidates.
    void commit(std::uint32_t field, const value& data); ///< Apply one validated distinct value.
    /// @brief Validate and copy a demand before queue publication.
    void enqueue(notification_kind kind, std::uint32_t index, std::span<const value> args,
                 value* owned_field = nullptr, mutation_kind mutation = mutation_kind::assign);
    ///< Strong admission guarantee for queue/counter state.
    /// @brief Reserve a retained observer outside notification delivery.
    subscription add(notification_kind kind, std::uint32_t index,
                     std::shared_ptr<observer> listener);
    ///< Reserve/reuse one observer entry outside flush.
    void deliver(const queued_command& command);       ///< Stable owner callback traversal.
    void admit(const command& demand);                 ///< Checked atomic-batch producer routing.
    schema description;                                ///< Shared immutable offsets/defaults.
    std::shared_ptr<const detail::schema_data> layout; ///< Direct validated metadata.
    runtime_limits limits;                             ///< Frame and lifetime budgets.
    std::shared_ptr<std::pmr::memory_resource> memory; ///< Outlives PMR storage.
    diagnostics::services destinations;                ///< Optional diagnostics.
    std::pmr::vector<std::uint8_t> booleans;           ///< Dense logical values, not bit proxies.
    std::pmr::vector<std::int64_t> integers;           ///< Dense signed integers.
    std::pmr::vector<double> numbers;                  ///< Dense finite doubles.
    std::pmr::vector<std::pmr::string> texts;          ///< Resource-aware variable-length strings.
    std::pmr::vector<queued_command> queue;            ///< Reserved command metadata.
    std::pmr::vector<value>
        staged; ///< Bounded argument copy scratch; supports forwarding callback spans.
    std::pmr::vector<value> arguments;      ///< Stable storage through callback-produced appends.
    std::pmr::vector<pending_field> latest; ///< Field-indexed pending write lookup.
    std::pmr::vector<subscription_bucket> buckets;         ///< Direct slot-to-observer lists.
    std::pmr::vector<registration> listeners;              ///< Reserved observer registrations.
    statistics stats;                                      ///< Frame-wide counters.
    std::size_t payload_bytes = 0, admitted_arguments = 0; ///< Frame-wide payload budgets.
    std::uint64_t segment = 1, sequence = 0; ///< Frame segments and lifetime subscription tokens.
    bool admitting = false;                  ///< Admission/diagnostic reentry guard.
    bool flushing = false;                   ///< Recursive flush/structural admission guard.
    view_model* facade = nullptr;            ///< Borrowed while the unique facade exists.
    const std::uint64_t instance = detail::next_identity();    ///< Token ownership identity.
    const std::thread::id thread = std::this_thread::get_id(); ///< Single serial owner.
};
} // namespace atlas::mvvm
