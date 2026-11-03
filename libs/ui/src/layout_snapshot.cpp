/** @file @brief Immutable geometry queries never reference live tree or algorithm buffers. */
#include "layout_state.hpp"
#include <algorithm>

namespace atlas::ui
{
/// @brief Only completed calculations construct nonempty publication packets.
layout_snapshot::layout_snapshot(std::shared_ptr<const detail::layout_snapshot_data> data)
    : data_(std::move(data))
{
}

/// @brief A moved-from packet has no source session.
std::uint64_t layout_snapshot::session() const noexcept
{
    return data_ ? data_->session : 0;
}

/// @brief A moved-from packet has no source revision.
std::uint64_t layout_snapshot::revision() const noexcept
{
    return data_ ? data_->revision : 0;
}

/// @brief Captured logical bounds do not change with a later native resize.
layout_rect layout_snapshot::viewport() const noexcept
{
    return data_ ? data_->viewport : layout_rect{};
}

/// @brief Borrow values only while a retained packet copy exists.
std::span<const layout_item> layout_snapshot::items() const noexcept
{
    return data_ ? std::span<const layout_item>{data_->items} : std::span<const layout_item>{};
}

/// @brief Full identity validation prevents generation/session aliasing in retained outputs.
std::optional<layout_geometry> layout_snapshot::find(element_handle target) const
{
    const auto values = items();
    const auto item = std::lower_bound(values.begin(), values.end(), target.index,
                                       [](const layout_item& value, std::uint32_t index)
                                       {
                                           return value.target.index < index;
                                       });
    return item != values.end() && item->target == target ? std::optional{item->geometry}
                                                          : std::nullopt;
}
} // namespace atlas::ui
