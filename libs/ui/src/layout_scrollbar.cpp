/** @file @brief Sparse immutable chrome output resolves only full live scroll-owner identities. */
#include "layout_state.hpp"
#include "snapshot_lookup.hpp"
#include <algorithm>

namespace atlas::ui::detail
{
/// @brief Removed, reused or converted viewport identities leave their chrome hidden and inert.
void prepare_scrollbars(const element_snapshot& source, layout_snapshot_data& output)
{
    output.scrollbars.reserve(source.scrollbars().size());
    for (const auto& link : source.scrollbars())
    {
        const auto owner = std::lower_bound(output.scrolls.begin(), output.scrolls.end(),
                                            link.style.viewport.index,
                                            [](const scroll_item& item, std::uint32_t index)
                                            {
                                                return item.target.index < index;
                                            });
        const auto track =
            std::lower_bound(output.items.begin(), output.items.end(), link.target.index,
                             [](const layout_item& item, std::uint32_t index)
                             {
                                 return item.target.index < index;
                             });
        scrollbar_geometry value;
        if (owner != output.scrolls.end() && owner->target == link.style.viewport &&
            track != output.items.end() && track->target == link.target &&
            !is_descendant(source, link.target, link.style.viewport, source.elements().size()) &&
            !is_descendant(source, link.style.viewport, link.target, source.elements().size()))
        {
            value = resolve_scrollbar(track->geometry.bounds, owner->geometry, link.style);
        }

        output.scrollbars.push_back({link.target, value});
    }
}
} // namespace atlas::ui::detail
