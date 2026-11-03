/**
 * @file
 * @brief Immutable calculated geometry tied to one tree layout-input revision.
 */
#pragma once
#include <atlas/ui/layout_types.hpp>
#include <optional>

namespace atlas::ui
{
namespace detail
{
class layout_snapshot_data; ///< Private allocator-retaining output storage.
}

/// @brief Owned result; other threads require synchronized publication before const reads.
class layout_snapshot
{
  public:
    layout_snapshot() = default;                         ///< Empty session/revision-zero result.
    std::uint64_t session() const noexcept;              ///< Captured tree identity.
    std::uint64_t revision() const noexcept;             ///< Captured geometry-input revision.
    layout_rect viewport() const noexcept;               ///< Captured logical viewport.
    std::span<const layout_item> items() const noexcept; ///< Ascending live slot order.
    std::optional<layout_geometry>
    find(element_handle target) const; ///< Reject stale/foreign identities.
  private:
    friend class layout_engine; ///< Only completed calculation publishes storage.
    explicit layout_snapshot(
        std::shared_ptr<const detail::layout_snapshot_data> data); ///< Retain output.
    std::shared_ptr<const detail::layout_snapshot_data> data_;     ///< Shared immutable values.
};
} // namespace atlas::ui
