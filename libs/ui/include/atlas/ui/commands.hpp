/**
 * @file
 * @brief Bounded serial UI command production and immutable phase-boundary batches.
 */
#pragma once
#include <atlas/diagnostics/instrumentation.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <memory_resource>
#include <span>
#include <stdexcept>
#include <variant>

namespace atlas::ui
{
/// @brief Session-local opaque identity; the future tree validates generation/liveness.
struct element_handle
{
    std::uint32_t index = 0;      ///< Slot zero is usable; generations distinguish reuse.
    std::uint32_t generation = 0; ///< Zero denotes an invalid identity.
    bool operator==(const element_handle&) const = default; ///< Compare complete identities.
};

/// @brief Initial absolute property vocabulary; actions and relative deltas are separate contracts.
enum class property : std::uint8_t
{
    width,   ///< Finite nonnegative requested width.
    height,  ///< Finite nonnegative requested height.
    opacity, ///< Finite normalized opacity in [0, 1].
    visible  ///< Boolean visibility request.
};

using property_value = std::variant<float, bool>; ///< Validated against the selected property.

/// @brief One admitted absolute write; ordering follows serial production, never worker arrival.
struct property_write
{
    element_handle target;      ///< Full identity, not an ECS pointer.
    property field;             ///< Addressed built-in property.
    property_value value;       ///< Owned scalar value.
    std::uint64_t producer = 0; ///< Nonzero stable identity assigned by the session.
    std::uint64_t sequence = 0; ///< One-based admission order within this frame.
};

/// @brief A repeated frame-local write; last-writer fallback remains an unresolved warning.
struct command_conflict
{
    property_write previous;           ///< Immediately preceding write to this key in frame order.
    property_write replacement;        ///< Last write, selected for the current batch.
    std::size_t writes_this_frame = 0; ///< Includes identical writes and earlier sealed batches.
};

/// @brief Explicit work bounds, independent of logger delivery/retention policy.
struct command_limits
{
    std::size_t max_commands =
        4096;                       ///< Total raw writes admitted per frame, including sealed ones.
    std::uint32_t max_batches = 32; ///< Maximum sync-boundary batches per frame.
    std::size_t max_warning_details = 4; ///< Per-batch details; zero retains only the summary.
};

namespace detail
{
class command_data; ///< Private immutable payload retaining its allocation resource.
}

/// @brief Owned immutable batch; const reads may run concurrently after safe publication.
class command_batch
{
  public:
    std::uint64_t frame() const noexcept;   ///< Zero on a moved-from batch.
    std::uint32_t ordinal() const noexcept; ///< One-based sync-boundary identity within the frame.
    std::span<const property_write>
    writes() const noexcept; ///< Winners in original sequence order.
    std::span<const command_conflict> conflicts() const noexcept; ///< Sorted by winning sequence.
  private:
    friend class command_buffer; ///< Only sealing can publish validated data.
    explicit command_batch(std::shared_ptr<const detail::command_data> data); ///< Retain payload.
    std::shared_ptr<const detail::command_data> data_; ///< Copies share immutable storage.
};

/// @brief Owner-thread command producer; no tree mutation, worker submission or implicit scheduler.
class command_buffer
{
  public:
    explicit command_buffer(command_limits limits = {},
                            std::shared_ptr<std::pmr::memory_resource> memory = {},
                            diagnostics::services destinations = {}); ///< Reserve bounded storage.
    ~command_buffer(); ///< Stop all callers before destruction; destroy on its owner thread.
    command_buffer(const command_buffer&) = delete; ///< One serial producer owns admission.
    command_buffer& operator=(const command_buffer&) = delete; ///< Ownership cannot be copied.
    void begin_frame(std::uint64_t frame); ///< Require increasing identity and no pending writes.
    void set(element_handle target, property field, property_value value,
             std::uint64_t producer); ///< Validate before appending; never mutate live UI state.
    command_batch
    seal(); ///< Strong exception guarantee; publish winners/conflicts, retain history.
    void discard_pending();      ///< Roll back only unsealed writes after a dispatch failure.
    std::size_t pending() const; ///< Owner-thread observation; no synchronization guarantee.
  private:
    class impl; ///< Frame history, owner guard, allocator and optional diagnostic endpoints.
    std::unique_ptr<impl> impl_; ///< Lives through all serial producer operations.
};
} // namespace atlas::ui
