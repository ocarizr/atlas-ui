/** @file @brief Tree session lifetime, validated identity and read-only observations. */
#include "tree_state.hpp"
#include <atomic>

namespace atlas::ui
{
namespace
{
/// @brief Exhaustion fails rather than wrapping into another live or retained session identity.
std::uint64_t new_session()
{
    static std::atomic<std::uint64_t> next{1};
    auto value = next.load(std::memory_order_relaxed);
    while (value != std::numeric_limits<std::uint64_t>::max())
    {
        if (next.compare_exchange_weak(value, value + 1, std::memory_order_relaxed))
        {
            return value;
        }
    }

    throw std::overflow_error("UI tree session identities exhausted");
}
} // namespace

/// @brief Fixed scratch permits subtree removal/invalidation without additional allocations.
element_tree::impl::impl(std::uint64_t id, tree_limits limits,
                         std::shared_ptr<std::pmr::memory_resource> resource,
                         diagnostics::services services)
    : session(id), memory(std::move(resource)), destinations(std::move(services)),
      slots(0, memory.get()), free(0, memory.get()), work(0, memory.get())
{
    slots.resize(limits.max_elements);
    free.reserve(limits.max_elements);
    work.reserve(limits.max_elements);
    for (auto index = limits.max_elements; index != 0; --index)
    {
        free.push_back(index - 1);
    }
}

/// @brief Read the immutable thread identity before accessing mutable state.
void element_tree::impl::check() const
{
    if (owner != std::this_thread::get_id())
    {
        throw std::logic_error("UI tree requires its owner thread");
    }

    if (busy)
    {
        throw std::logic_error("UI tree operation cannot be reentered");
    }
}

/// @brief Session and generation together prevent accidental cross-tree identity aliasing.
bool element_tree::impl::live(element_handle target) const noexcept
{
    return target.session == session && target.index < slots.size() &&
           slots[target.index].entity != entt::null &&
           slots[target.index].generation == target.generation;
}

/// @brief Never dereference an unvalidated public identity.
std::uint32_t element_tree::impl::require(element_handle target) const
{
    if (!live(target))
    {
        throw std::invalid_argument("UI tree target is stale, invalid or from another session");
    }

    return target.index;
}

/// @brief Missing hierarchy links use the canonical empty public identity.
element_handle element_tree::impl::handle(std::uint32_t index) const noexcept
{
    return index == none ? element_handle{}
                         : element_handle{index, slots[index].generation, session};
}

/// @brief Registry access remains private and serialized by the tree owner.
element_tree::impl::node& element_tree::impl::at(std::uint32_t index)
{
    return registry.get<node>(slots[index].entity);
}

/// @brief Const access does not imply permission for concurrent registry reads.
const element_tree::impl::node& element_tree::impl::at(std::uint32_t index) const
{
    return registry.get<node>(slots[index].entity);
}

/// @brief Applications choose PMR policy; EnTT entity/component allocations are separate.
element_tree::element_tree(tree_limits limits, std::shared_ptr<std::pmr::memory_resource> memory,
                           diagnostics::services destinations)
{
    if (limits.max_elements == 0 || limits.max_elements > 65536)
    {
        throw std::invalid_argument("UI tree capacity must be in 1..65536");
    }

    if (!memory)
    {
        memory = {std::pmr::new_delete_resource(), [](std::pmr::memory_resource*)
                  {
                  }};
    }

    impl_ = std::make_unique<impl>(new_session(), limits, memory, destinations);
    impl_->commands.reset(new command_buffer{impl_->session, &impl_->busy, limits.commands,
                                             std::move(memory), std::move(destinations)});
    const impl::guard guard{impl_->busy};
    diagnostics::write(impl_->destinations, diagnostics::log_level::debug, "ui.tree",
                       "tree initialized");
}

/// @brief Owner-affine destruction must not be invoked from a diagnostic callback.
element_tree::~element_tree()
{
    if (impl_->owner != std::this_thread::get_id() || impl_->busy)
    {
        std::terminate();
    }

    const impl::guard guard{impl_->busy};
    diagnostics::write(impl_->destinations, diagnostics::log_level::debug, "ui.tree",
                       "tree released");
}

/// @brief Queries return values, not mutable component pointers.
bool element_tree::contains(element_handle target) const
{
    impl_->check();
    return impl_->live(target);
}

/// @brief Capacity and live occupancy are distinct.
std::size_t element_tree::size() const
{
    impl_->check();
    return impl_->count;
}

/// @brief Observe only committed topology.
element_handle element_tree::parent(element_handle target) const
{
    impl_->check();
    return impl_->handle(impl_->at(impl_->require(target)).parent);
}

/// @brief Traverse children in creation/reparent insertion order.
element_handle element_tree::first_child(element_handle target) const
{
    impl_->check();
    return impl_->handle(impl_->at(impl_->require(target)).first);
}

/// @brief Independent roots have no sibling chain.
element_handle element_tree::next_sibling(element_handle target) const
{
    impl_->check();
    return impl_->handle(impl_->at(impl_->require(target)).next);
}

/// @brief Sealed and unsealed commands remain invisible until apply succeeds.
requested_properties element_tree::requested(element_handle target) const
{
    impl_->check();
    return impl_->at(impl_->require(target)).requested;
}

/// @brief Invalidation accumulates until explicit acknowledgment.
invalidation element_tree::dirty(element_handle target) const
{
    impl_->check();
    return impl_->at(impl_->require(target)).dirty;
}

/// @brief Consumers acknowledge only work they have actually processed.
void element_tree::acknowledge(element_handle target)
{
    impl_->check();
    impl_->at(impl_->require(target)).dirty = {false, false};
}

/// @brief A tree has one serial stream, preserving frame-wide conflict history.
command_buffer& element_tree::commands()
{
    impl_->check();
    return *impl_->commands;
}
} // namespace atlas::ui
