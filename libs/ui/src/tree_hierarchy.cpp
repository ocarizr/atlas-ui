/** @file @brief Validated hierarchy edits and iterative conservative invalidation. */
#include "tree_state.hpp"

namespace atlas::ui
{
/// @brief Remove sibling links without allocating or changing descendants.
void element_tree::impl::detach(std::uint32_t index)
{
    auto& value = at(index);
    if (value.parent != none)
    {
        auto& parent = at(value.parent);
        if (value.previous == none)
        {
            parent.first = value.next;
        }
        else
        {
            at(value.previous).next = value.next;
        }

        if (value.next == none)
        {
            parent.last = value.previous;
        }
        else
        {
            at(value.next).previous = value.previous;
        }
    }

    value.parent = value.previous = value.next = none;
}

/// @brief Link only after all validation/allocation succeeds.
void element_tree::impl::attach(std::uint32_t index, std::uint32_t parent_index)
{
    auto& value = at(index);
    value.parent = parent_index;
    if (parent_index != none)
    {
        auto& parent = at(parent_index);
        value.previous = parent.last;
        if (parent.last == none)
        {
            parent.first = index;
        }
        else
        {
            at(parent.last).next = index;
        }

        parent.last = index;
    }
}

/// @brief Layout changes can affect measured ancestry without dirtying unrelated branches.
void element_tree::impl::ancestors(std::uint32_t index)
{
    while (index != none)
    {
        auto& value = at(index);
        value.dirty = {true, true};
        value.computed_valid = false;
        index = value.parent;
    }
}

/// @brief Inherited placement/visual changes conservatively affect all descendants.
void element_tree::impl::subtree(std::uint32_t index, bool layout)
{
    work.clear();
    work.push_back(index);
    for (std::size_t cursor = 0; cursor < work.size(); ++cursor)
    {
        auto& value = at(work[cursor]);
        value.dirty.paint = true;
        value.dirty.layout = value.dirty.layout || layout;
        if (layout)
        {
            value.computed_valid = false;
        }

        for (auto child = value.first; child != none; child = at(child).next)
        {
            work.push_back(child);
        }
    }
}

/// @brief Failed registry construction leaves the public slot and topology unchanged.
element_handle element_tree::create(element_handle parent)
{
    impl_->check();
    const impl::guard guard{impl_->busy};
    const diagnostics::operation_scope scope{impl_->destinations, {"ui.tree.create", "ui.cpu"}};
    const auto parent_index = parent == element_handle{} ? impl::none : impl_->require(parent);
    impl_->validate_parent(parent_index);
    impl_->ensure_layout_revision();
    if (impl_->free.empty())
    {
        throw std::overflow_error("UI tree element capacity exhausted");
    }

    const auto index = impl_->free.back();
    const auto entity = impl_->registry.create();
    try
    {
        impl_->registry.emplace<impl::node>(entity);
    }
    catch (...)
    {
        impl_->registry.destroy(entity);
        throw;
    }

    impl_->slots[index].entity = entity;
    impl_->free.pop_back();
    ++impl_->count;
    impl_->attach(index, parent_index);
    impl_->ancestors(parent_index);
    ++impl_->layout_revision;
    return impl_->handle(index);
}

/// @brief A bounded ancestor walk validates cycles before any structural changes.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) -- Conventional target, new-parent order.
void element_tree::reparent(element_handle target, element_handle parent)
{
    impl_->check();
    const impl::guard guard{impl_->busy};
    const diagnostics::operation_scope scope{impl_->destinations, {"ui.tree.reparent", "ui.cpu"}};
    const auto index = impl_->require(target);
    const auto parent_index = parent == element_handle{} ? impl::none : impl_->require(parent);
    for (auto ancestor = parent_index; ancestor != impl::none;
         ancestor = impl_->at(ancestor).parent)
    {
        if (ancestor == index)
        {
            throw std::invalid_argument("UI tree hierarchy cycle");
        }
    }

    const auto previous = impl_->at(index).parent;
    if (previous == parent_index)
    {
        return;
    }

    impl_->validate_parent(parent_index);
    impl_->ensure_layout_revision();
    impl_->detach(index);
    impl_->attach(index, parent_index);
    ++impl_->layout_revision;
    impl_->ancestors(previous);
    impl_->ancestors(parent_index);
    impl_->subtree(index, true);
}

/// @brief Collect descendants before destruction; recycled identities always advance generation.
void element_tree::remove(element_handle target)
{
    impl_->check();
    const impl::guard guard{impl_->busy};
    const diagnostics::operation_scope scope{impl_->destinations, {"ui.tree.remove", "ui.cpu"}};
    const auto index = impl_->require(target);
    const auto parent = impl_->at(index).parent;
    impl_->ensure_layout_revision();
    impl_->subtree(index, true);
    impl_->detach(index);
    impl_->ancestors(parent);
    for (auto removed : impl_->work)
    {
        auto& slot = impl_->slots[removed];
        impl_->registry.destroy(slot.entity);
        slot.entity = entt::null;
        if (slot.generation != std::numeric_limits<std::uint32_t>::max())
        {
            ++slot.generation;
            impl_->free.push_back(removed);
        }

        --impl_->count;
    }

    ++impl_->layout_revision;
}
} // namespace atlas::ui
