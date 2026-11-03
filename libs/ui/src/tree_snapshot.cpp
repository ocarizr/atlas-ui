/** @file @brief Failure-safe copied inputs without worker access to the live registry. */
#include "tree_state.hpp"

namespace atlas::ui
{
/// @brief Container destruction precedes release of the allocation policy.
class detail::element_snapshot_data
{
  public:
    element_snapshot_data(std::uint64_t id, std::shared_ptr<std::pmr::memory_resource> resource)
        : session(id), memory(std::move(resource)), records(0, memory.get())
    {
    } ///< Counted construction permits debug metadata allocation failure.

    const std::uint64_t session; ///< Retained capture identity, not tree ownership.
    std::shared_ptr<std::pmr::memory_resource> memory; ///< Outlives PMR record destruction.
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

/// @brief Capture is owner-only; failed allocation never acknowledges or mutates live state.
element_snapshot element_tree::snapshot() const
{
    impl_->check();
    const impl::guard guard{impl_->busy};
    const diagnostics::operation_scope scope{impl_->destinations, {"ui.tree.snapshot", "ui.cpu"}};
    auto data = std::make_shared<detail::element_snapshot_data>(impl_->session, impl_->memory);
    data->records.reserve(impl_->count);
    for (std::uint32_t index = 0; index < impl_->slots.size(); ++index)
    {
        if (impl_->slots[index].entity == entt::null)
        {
            continue;
        }

        const auto& node = impl_->at(index);
        data->records.push_back({impl_->handle(index), impl_->handle(node.parent),
                                 impl_->handle(node.first), impl_->handle(node.next),
                                 node.requested, node.dirty});
    }

    return element_snapshot{std::move(data)};
}
} // namespace atlas::ui
