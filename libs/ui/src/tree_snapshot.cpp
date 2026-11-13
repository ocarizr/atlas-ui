/** @file @brief Failure-safe copied inputs without worker access to the live registry. */
#include "tree_state.hpp"

namespace atlas::ui
{
/// @brief Container destruction precedes release of the allocation policy.
class detail::element_snapshot_data
{
  public:
    element_snapshot_data(std::uint64_t id, std::shared_ptr<std::pmr::memory_resource> resource)
        : session(id), memory(std::move(resource)), canvas_slots(0, memory.get()),
          stack_slots(0, memory.get()), boundaries(0, memory.get()), records(0, memory.get())
    {
    } ///< Counted construction permits debug metadata allocation failure.

    std::uint64_t revision = 0;  ///< Geometry-input revision at capture.
    layout_rect viewport{};      ///< Logical root bounds at capture.
    const std::uint64_t session; ///< Retained capture identity, not tree ownership.
    std::shared_ptr<std::pmr::memory_resource> memory; ///< Outlives PMR record destruction.
    std::pmr::vector<canvas_slot_record> canvas_slots; ///< Sparse explicit placement overrides.
    std::pmr::vector<stack_slot_record> stack_slots;   ///< Sparse explicit stack metadata.
    std::pmr::vector<boundary_record> boundaries;      ///< Sparse directed sibling dependencies.
    std::pmr::vector<element_record> records;          ///< Copied in stable slot order.
};

/// @brief Publish only completely prepared records.
element_snapshot::element_snapshot(std::shared_ptr<const detail::element_snapshot_data> data)
    : data_(std::move(data))
{
}

/// @brief Retained records distinguish sessions even after source destruction.
std::uint64_t element_snapshot::session() const noexcept
{
    return data_ ? data_->session : 0;
}

/// @brief Borrow only while at least one snapshot copy is alive.
std::span<const element_record> element_snapshot::elements() const noexcept
{
    return data_ ? std::span<const element_record>{data_->records}
                 : std::span<const element_record>{};
}

/// @brief Default slots have no explicit record; retained overrides outlive the source tree.
std::span<const canvas_slot_record> element_snapshot::canvas_slots() const noexcept
{
    return data_ ? std::span<const canvas_slot_record>{data_->canvas_slots}
                 : std::span<const canvas_slot_record>{};
}

/// @brief Default stack weights/order are implicit and consume no sparse record.
std::span<const stack_slot_record> element_snapshot::stack_slots() const noexcept
{
    return data_ ? std::span<const stack_slot_record>{data_->stack_slots}
                 : std::span<const stack_slot_record>{};
}

/// @brief Relationship identities remain valid within the retained input after tree destruction.
std::span<const boundary_record> element_snapshot::boundaries() const noexcept
{
    return data_ ? std::span<const boundary_record>{data_->boundaries}
                 : std::span<const boundary_record>{};
}

/// @brief Capture is owner-only; failed allocation never acknowledges or mutates live state.
element_snapshot element_tree::snapshot() const
{
    impl_->check();
    const impl::guard guard{impl_->busy};
    const diagnostics::operation_scope scope{impl_->destinations, {"ui.tree.snapshot", "ui.cpu"}};
    auto data = std::make_shared<detail::element_snapshot_data>(impl_->session, impl_->memory);
    data->revision = impl_->layout_revision;
    data->viewport = impl_->viewport;
    data->records.reserve(impl_->count);
    const auto& registry = impl_->registry;
    data->canvas_slots.reserve(registry.view<const canvas_slot>().size());
    data->stack_slots.reserve(registry.view<const stack_slot>().size());
    data->boundaries.reserve(registry.view<const layout_boundaries>().size());
    for (std::uint32_t index = 0; index < impl_->slots.size(); ++index)
    {
        if (impl_->slots[index].entity == entt::null)
        {
            continue;
        }

        const auto& node = impl_->at(index);
        data->records.push_back({impl_->handle(index), impl_->handle(node.parent),
                                 impl_->handle(node.first), impl_->handle(node.next),
                                 node.requested, node.dirty, node.layout, node.panel,
                                 impl_->child_visual(index)});
        if (const auto* value = registry.try_get<paint_style>(impl_->slots[index].entity))
        {
            data->records.back().paint = *value;
        }

        auto& style = data->records.back().paint;
        style.translation = node.translation;
        style.scale = node.scale;
        style.rotation = node.rotation;

        if (const auto* slot = registry.try_get<canvas_slot>(impl_->slots[index].entity))
        {
            data->canvas_slots.push_back({impl_->handle(index), *slot});
        }

        if (const auto* slot = registry.try_get<stack_slot>(impl_->slots[index].entity))
        {
            data->stack_slots.push_back({impl_->handle(index), *slot});
        }

        if (const auto* value = registry.try_get<layout_boundaries>(impl_->slots[index].entity))
        {
            data->boundaries.push_back({impl_->handle(index), *value});
        }
    }

    return element_snapshot{std::move(data)};
}

/// @brief Input revision does not change for paint-only updates.
std::uint64_t element_snapshot::revision() const noexcept
{
    return data_ ? data_->revision : 0;
}

/// @brief Empty snapshots have an empty viewport.
layout_rect element_snapshot::viewport() const noexcept
{
    return data_ ? data_->viewport : layout_rect{};
}
} // namespace atlas::ui
