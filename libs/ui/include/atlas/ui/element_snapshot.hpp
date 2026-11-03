/**
 * @file
 * @brief Owned immutable element inputs for workers and sequential-reference execution.
 */
#pragma once
#include <atlas/ui/element_state.hpp>
#include <atlas/ui/layout_slot.hpp>

namespace atlas::ui
{
namespace detail
{
class element_snapshot_data; ///< Private allocator-retaining copied storage.
}

/// @brief Safe const reads after synchronized publication; does not retain a mutable tree.
class element_snapshot
{
  public:
    element_snapshot() = default;           ///< Empty identity-zero snapshot.
    std::uint64_t session() const noexcept; ///< Capture session, or zero for empty/moved-from.
    std::span<const element_record> elements() const noexcept; ///< Ascending live slot order.
    std::uint64_t revision() const noexcept; ///< Geometry-input revision, independent of paint.
    layout_rect viewport() const noexcept;   ///< Available logical root rectangle.
    std::span<const canvas_slot_record>
    canvas_slots() const noexcept; ///< Explicit sparse overrides.
  private:
    friend class element_tree; ///< Only owner-thread capture can read registry state.
    explicit element_snapshot(
        std::shared_ptr<const detail::element_snapshot_data> data); ///< Retain immutable storage.
    std::shared_ptr<const detail::element_snapshot_data> data_; ///< Copies share owned input bytes.
};
} // namespace atlas::ui
