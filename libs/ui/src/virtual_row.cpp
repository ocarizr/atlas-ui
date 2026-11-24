/** @file @brief Isolated row construction and subscription ownership without stale state reuse. */
#include "virtual_list_state.hpp"

namespace atlas::ui
{
/// @brief Borrow one private row builder for the duration of its serial factory call.
row_context::row_context(detail::row_builder& state) : state_(state)
{
}

/// @brief Expose only the unpublished draft to the row factory.
element_tree& row_context::tree() const noexcept
{
    return state_.transaction.draft();
}

/// @brief Return the draft row root rather than a mutable live-tree identity.
element_handle row_context::root() const noexcept
{
    return state_.root;
}

/// @brief Expose immutable logical metadata and the candidate binding ticket.
const realized_row& row_context::row() const noexcept
{
    return state_.value;
}

/// @brief Validate row-local interaction ownership before staged binding admission.
void row_context::bind(element_handle target, std::shared_ptr<input_handler> handler,
                       interaction_options options)
{
    state_.bind(target, std::move(handler), options);
}

/// @brief Bound and retain resource ownership through input lifecycle cleanup.
void row_context::retain(std::shared_ptr<void> resource)
{
    if (!resource)
    {
        throw std::invalid_argument("virtual row resource cannot be empty");
    }

    if (state_.lease->resources.size() >= state_.limits.max_nodes_per_row)
    {
        throw std::overflow_error("virtual row retained resource admission exhausted");
    }

    state_.lease->resources.push_back(std::move(resource));
}
} // namespace atlas::ui

namespace atlas::ui::detail
{
/// @brief Associate one draft subtree with its candidate identity and resource lease.
row_builder::row_builder(tree_edit& edit, input_edit& bindings, realized_row row,
                         element_handle target, std::shared_ptr<row_lease> retained,
                         std::shared_ptr<virtual_navigation> state, virtual_list_limits policy)
    : transaction(edit), input(bindings), value(row), root(target), lease(std::move(retained)),
      navigation(std::move(state)), limits(policy)
{
}

/// @brief Invoke the factory once and supply a default focusable root when needed.
void row_builder::build(const virtual_list::factory& create)
{
    row_context context{*this};
    create(context);
    if (!root_bound)
    {
        bind(root, {}, {true, true, true});
    }
}

/// @brief Validate row-local interaction ownership before staged binding admission.
void row_builder::bind(element_handle target, std::shared_ptr<input_handler> handler,
                       interaction_options options)
{
    auto& tree = transaction.draft();
    auto ancestor = target;
    while (ancestor.generation && ancestor != root)
    {
        ancestor = tree.parent(ancestor);
    }

    if (ancestor != root)
    {
        throw std::invalid_argument("binding is outside its virtual row");
    }

    if (lease->bindings.size() >= limits.max_bindings_per_row)
    {
        throw std::overflow_error("virtual row binding admission exhausted");
    }

    if (target == root)
    {
        options.focusable = true;
        root_bound = true;
    }

    const auto published = transaction.published(target);
    lease->bindings.push_back(published);
    input.add(published, row_handler(std::move(handler), navigation, value.item.key, lease),
              options);
}
} // namespace atlas::ui::detail
