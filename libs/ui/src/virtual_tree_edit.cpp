/** @file @brief Failure-atomic registry staging with bounded live-tree cloning. */
#include "virtual_tree_edit.hpp"
#include <algorithm>

namespace atlas::ui::detail
{
namespace
{
/// @brief Copy optional sparse components without exposing EnTT in a public header.
template <class Component>
void copy_component(const entt::registry& source, entt::entity source_entity,
                    entt::registry& destination, entt::entity target_entity)
{
    if (const auto* value = source.try_get<Component>(source_entity))
    {
        destination.emplace<Component>(target_entity, *value);
    }
}
} // namespace

/// @brief Clone bounded live registry state into a distinct unpublished draft session.
tree_edit::tree_edit(element_tree& tree) : tree_(tree)
{
    auto& source = *tree_.impl_;
    source.check();
    draft_ = std::make_unique<element_tree>(
        tree_limits{static_cast<std::uint32_t>(source.slots.size())}, source.memory);
    auto& target = *draft_->impl_;
    target.free = source.free;
    target.slots = source.slots;
    for (auto& slot : target.slots)
    {
        slot.entity = entt::null;
    }

    for (std::size_t i = 0; i < source.slots.size(); ++i)
    {
        const auto original = source.slots[i].entity;
        if (original == entt::null)
        {
            continue;
        }

        const auto entity = target.registry.create();
        target.slots[i].entity = entity;
        target.registry.emplace<element_tree::impl::node>(
            entity, source.registry.get<element_tree::impl::node>(original));
        copy_component<paint_style>(source.registry, original, target.registry, entity);
        copy_component<canvas_slot>(source.registry, original, target.registry, entity);
        copy_component<stack_slot>(source.registry, original, target.registry, entity);
        copy_component<visual_style>(source.registry, original, target.registry, entity);
        copy_component<layout_boundaries>(source.registry, original, target.registry, entity);
        copy_component<scrollbar_style>(source.registry, original, target.registry, entity);
    }

    target.count = source.count;
    target.viewport = source.viewport;
    target.layout_revision = source.layout_revision;
    translate(*draft_, target.session);
    source.busy = true;
}

/// @brief Release the original tree guard before destroying the private draft.
tree_edit::~tree_edit()
{
    tree_.impl_->busy = false;
}

/// @brief Validate and translate a live identity into the private draft session.
element_handle tree_edit::staged(element_handle target) const
{
    if (target.session != tree_.impl_->session)
    {
        throw std::invalid_argument("foreign live row");
    }

    target.session = draft_->impl_->session;
    if (!draft_->contains(target))
    {
        throw std::invalid_argument("stale live row");
    }

    return target;
}

/// @brief Validate a draft identity and return its future live-session identity.
element_handle tree_edit::published(element_handle target) const
{
    if (!draft_->contains(target))
    {
        throw std::invalid_argument("foreign staged row");
    }

    target.session = tree_.impl_->session;
    return target;
}

/// @brief Rebase sparse cross-node identity links before registry publication.
void tree_edit::translate(element_tree& tree, std::uint64_t session)
{
    auto& registry = tree.impl_->registry;
    for (auto&& [entity, value] : registry.view<layout_boundaries>().each())
    {
        (void)entity;
        if (value.horizontal)
        {
            value.horizontal->source.session = session;
        }

        if (value.vertical)
        {
            value.vertical->source.session = session;
        }
    }

    for (auto&& [entity, value] : registry.view<scrollbar_style>().each())
    {
        (void)entity;
        value.viewport.session = session;
    }
}

/// @brief Logical ordering must survive slot reuse and stable-key reordering.
void tree_edit::order_children(element_handle parent, std::span<const element_handle> children)
{
    auto& state = *draft_->impl_;
    const auto parent_index = state.require(parent);
    std::size_t count = 0;
    for (auto child = state.at(parent_index).first; child != element_tree::impl::none;
         child = state.at(child).next)
    {
        ++count;
    }

    if (count != children.size())
    {
        throw std::invalid_argument("incomplete virtual row order");
    }

    for (std::size_t i = 0; i < children.size(); ++i)
    {
        const auto index = state.require(children[i]);
        if (state.at(index).parent != parent_index ||
            std::find(children.begin(), children.begin() + static_cast<std::ptrdiff_t>(i),
                      children[i]) != children.begin() + static_cast<std::ptrdiff_t>(i))
        {
            throw std::invalid_argument("invalid virtual row permutation");
        }
    }

    state.ensure_layout_revision();
    auto& container = state.at(parent_index);
    container.first = children.empty() ? element_tree::impl::none : children.front().index;
    container.last = children.empty() ? element_tree::impl::none : children.back().index;
    for (std::size_t i = 0; i < children.size(); ++i)
    {
        auto& node = state.at(children[i].index);
        node.previous = i ? children[i - 1].index : element_tree::impl::none;
        node.next = i + 1 < children.size() ? children[i + 1].index : element_tree::impl::none;
    }

    ++state.layout_revision;
}

/// @brief Publish completely prepared state without allocating or invoking user callbacks.
void tree_edit::commit()
{
    if (committed_)
    {
        throw std::logic_error("tree edit already committed");
    }

    auto& live = *tree_.impl_;
    auto& draft = *draft_->impl_;
    translate(*draft_, live.session);
    live.registry.swap(draft.registry);
    live.slots.swap(draft.slots);
    live.free.swap(draft.free);
    live.work.swap(draft.work);
    live.count = draft.count;
    live.layout_revision = draft.layout_revision;
    live.viewport = draft.viewport;
    committed_ = true;
}
} // namespace atlas::ui::detail
