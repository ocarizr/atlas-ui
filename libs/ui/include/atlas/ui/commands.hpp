/**
 * @file
 * @brief Bounded serial UI command production and immutable phase-boundary batches.
 */
#pragma once
#include <array>
#include <atlas/diagnostics/instrumentation.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <memory_resource>
#include <span>
#include <stdexcept>
#include <string_view>
#include <variant>

namespace atlas::ui
{
class element_tree; ///< Owner of session-bound elements and one serial command stream.

/// @brief Opaque identity; a tree validates its session, slot and generation together.
struct element_handle
{
    std::uint32_t index = 0;      ///< Slot zero is usable; generations distinguish reuse.
    std::uint32_t generation = 0; ///< Zero denotes an invalid identity.
    std::uint64_t session = 0;    ///< Zero is reserved for standalone, unbound command production.
    bool operator==(const element_handle&) const = default; ///< Compare complete identities.
};

/// @brief Initial absolute property vocabulary; actions and relative deltas are separate contracts.
enum class property : std::uint8_t
{
    width,               ///< Finite nonnegative requested width.
    height,              ///< Finite nonnegative requested height.
    opacity,             ///< Finite normalized opacity in [0, 1].
    visible,             ///< Boolean visibility request.
    translate_x,         ///< Signed horizontal visual offset in logical units.
    translate_y,         ///< Signed vertical visual offset in logical units.
    scale_x,             ///< Nonnegative horizontal scale about the border origin.
    scale_y,             ///< Nonnegative vertical scale about the border origin.
    rotate,              ///< Clockwise angle in degrees.
    margin_left,         ///< Leading outer inset in logical units.
    margin_top,          ///< Upper outer inset in logical units.
    margin_right,        ///< Trailing outer inset in logical units.
    margin_bottom,       ///< Lower outer inset in logical units.
    padding_left,        ///< Leading inner inset in logical units.
    padding_top,         ///< Upper inner inset in logical units.
    padding_right,       ///< Trailing inner inset in logical units.
    padding_bottom,      ///< Lower inner inset in logical units.
    spacing,             ///< Stack spacing in logical units.
    canvas_x_offset_min, ///< Leading horizontal canvas offset.
    canvas_x_offset_max, ///< Trailing horizontal canvas offset.
    canvas_y_offset_min, ///< Leading vertical canvas offset.
    canvas_y_offset_max, ///< Trailing vertical canvas offset.
    canvas_x_anchor_min, ///< Leading normalized horizontal anchor.
    canvas_x_anchor_max, ///< Trailing normalized horizontal anchor.
    canvas_y_anchor_min, ///< Leading normalized vertical anchor.
    canvas_y_anchor_max, ///< Trailing normalized vertical anchor.
};

/// @brief Validate a finite numeric value for the given scalar property.
[[nodiscard]] bool valid_scalar(property field, float value) noexcept;
/// @brief Return the diagnostic name of a validated property.
[[nodiscard]] std::string_view property_name(property field) noexcept;

using property_value =
    std::variant<float, bool, std::monostate>; ///< Monostate restores automatic width/height.

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
    std::uint64_t session() const noexcept; ///< Owning tree identity, or zero for unbound packets.
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
    void require_capacity(std::size_t writes) const; ///< Preflight a group of validated writes.
    command_batch
    seal(); ///< Strong exception guarantee; publish winners/conflicts, retain history.
    void discard_pending();      ///< Roll back only unsealed writes after a dispatch failure.
    std::size_t pending() const; ///< Owner-thread observation; no synchronization guarantee.
  private:
    friend class element_tree; ///< The tree binds its sole producer without exposing session
                               ///< forgery.
    command_buffer(std::uint64_t session, const bool* tree_busy, command_limits limits,
                   std::shared_ptr<std::pmr::memory_resource> memory,
                   diagnostics::services destinations); ///< Retain the tree's admission binding.
    class impl; ///< Frame history, owner guard, allocator and optional diagnostic endpoints.
    std::unique_ptr<impl> impl_; ///< Lives through all serial producer operations.
};
} // namespace atlas::ui
