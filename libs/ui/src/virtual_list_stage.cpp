/** @file @brief Bounded row replacement and exact canvas placement before atomic publication. */
#include "virtual_list_state.hpp"
#include <cmath>

namespace atlas::ui
{
namespace
{
/// @brief Strict row boxes keep application child sizing from changing logical source geometry.
void position_row(element_tree& tree, element_handle target, const logical_item& item)
{
    auto style = tree.layout(target);
    style.margin = {};
    style.constraints.minimum.height = item.height;
    style.constraints.maximum.height = item.height;
    style.intrinsic.height = item.height;
    tree.set_layout(target, style);
    canvas_slot slot;
    slot.horizontal.anchor_max = 1;
    slot.horizontal.sizing = canvas_sizing::stretch;
    slot.vertical.offset_min = item.offset;
    tree.set_canvas_slot(target, slot);
}
} // namespace

/// @brief Replace expired row generations and place retained rows using exact logical extents.
element_handle
virtual_list::impl::stage(detail::tree_edit& edit, detail::input_edit& bindings,
                          std::span<realized_row> plan, float extent,
                          std::pmr::vector<std::shared_ptr<detail::row_lease>>& retained)
{
    auto& draft = edit.draft();
    auto canvas = content.generation ? edit.staged(content) : draft.create(edit.staged(viewport));
    if (!content.generation)
    {
        draft.set_panel(canvas, canvas_layout{});
        bindings.add(viewport, detail::navigation_handler(navigation), {true, true, true});
        bindings.manage_anchor(viewport);
    }

    auto style = draft.layout(canvas);
    style.intrinsic.height = extent;
    draft.set_layout(canvas, style);
    for (std::size_t i = 0; i < rows.size(); ++i)
    {
        const auto& previous = rows[i];
        const bool keep = std::any_of(plan.begin(), plan.end(),
                                      [&](const auto& row)
                                      {
                                          return row.item.key == previous.item.key &&
                                                 row.item.revision == previous.item.revision;
                                      });
        if (!keep)
        {
            for (auto target : leases[i]->bindings)
            {
                bindings.retire(target);
            }

            draft.remove(edit.staged(previous.ticket.root));
        }
    }

    float previous_end = 0;
    std::pmr::vector<element_handle> ordered(0, memory.get());
    ordered.reserve(plan.size());
    for (auto& row : plan)
    {
        if (!row.item.key || !std::isfinite(row.item.offset) || row.item.offset < previous_end ||
            !std::isfinite(row.item.height) || row.item.height <= 0 ||
            row.item.offset + row.item.height > extent)
        {
            throw std::invalid_argument("invalid virtual source row geometry");
        }

        previous_end = row.item.offset + row.item.height;
        const auto old = std::find_if(rows.begin(), rows.end(),
                                      [&](const auto& value)
                                      {
                                          return value.item.key == row.item.key &&
                                                 value.item.revision == row.item.revision;
                                      });
        if (old != rows.end())
        {
            row.ticket = old->ticket;
            retained.push_back(leases[static_cast<std::size_t>(old - rows.begin())]);
        }
        else
        {
            if (serial == std::numeric_limits<std::uint64_t>::max())
            {
                throw std::overflow_error("virtual binding generations exhausted");
            }

            const auto before = draft.size();
            const auto root = draft.create(canvas);
            row.ticket = {row.item.key, ++serial, edit.published(root)};
            auto lease = std::make_shared<detail::row_lease>(memory);
            detail::row_builder builder{edit, bindings, row, root, lease, navigation, limits};
            builder.build(create);
            if (draft.size() - before > limits.max_nodes_per_row)
            {
                throw std::overflow_error("virtual row node admission exhausted");
            }

            retained.push_back(std::move(lease));
        }

        position_row(draft, edit.staged(row.ticket.root), row.item);
        ordered.push_back(edit.staged(row.ticket.root));
    }

    edit.order_children(canvas, ordered);

    return edit.published(canvas);
}
} // namespace atlas::ui
