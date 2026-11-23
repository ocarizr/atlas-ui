/** @file @brief Owner-affine bounded list API and explicit lifetime boundaries. */
#include "virtual_list_state.hpp"
#include <cmath>

namespace atlas::ui
{
/// @brief Validate ownership and policy, then reserve bounded realization bookkeeping.
virtual_list::impl::impl(element_tree& source_tree, input_session& source_input,
                         element_handle scroll_owner, jobs::job_dispatcher dispatcher,
                         factory build, virtual_list_limits policy,
                         std::shared_ptr<std::pmr::memory_resource> resource,
                         diagnostics::services services)
    : tree(source_tree), input(source_input), viewport(scroll_owner), create(std::move(build)),
      limits(policy),
      memory(resource ? std::move(resource)
                      : std::shared_ptr<std::pmr::memory_resource>{std::pmr::new_delete_resource(),
                                                                   [](std::pmr::memory_resource*)
                                                                   {
                                                                   }}),
      destinations(std::move(services)),
      work(std::move(dispatcher),
           frame_limits{policy.max_realized, policy.max_realized * sizeof(realized_row), 1,
                        policy.min_parallel_items, policy.grain, policy.max_chunks},
           memory, destinations),
      rows(0, memory.get()), leases(0, memory.get()),
      navigation(std::make_shared<detail::virtual_navigation>())
{
    if (!create || !policy.max_realized || policy.max_realized > 32768 ||
        policy.overscan > policy.max_realized / 2 || !policy.max_nodes_per_row ||
        !policy.max_bindings_per_row || policy.max_bindings_per_row > policy.max_nodes_per_row)
    {
        throw std::invalid_argument("invalid virtual list admission limits");
    }

    const auto container = tree.panel(viewport);
    const auto* scroll = std::get_if<scroll_layout>(&container);
    if (!scroll || !scroll->vertical || scroll->horizontal || tree.first_child(viewport).generation)
    {
        throw std::invalid_argument("virtual list requires an empty vertical-only scroll owner");
    }

    detail::input_edit::require_idle(input, viewport.session);

    rows.reserve(policy.max_realized);
    leases.reserve(policy.max_realized);
}

/// @brief Reject worker access, foreign owners and reentrant realization.
void virtual_list::impl::check() const
{
    jobs::require_coordinator_thread();
    if (owner != std::this_thread::get_id() || busy || !navigation->active)
    {
        throw std::logic_error("virtual list requires its active non-reentrant owner");
    }
}

/// @brief Compose a bounded list over borrowed tree, input and shared dispatcher.
virtual_list::virtual_list(element_tree& tree, input_session& input, element_handle viewport,
                           jobs::job_dispatcher dispatcher, factory create,
                           virtual_list_limits limits,
                           std::shared_ptr<std::pmr::memory_resource> memory,
                           diagnostics::services destinations)
    : impl_(std::make_unique<impl>(tree, input, viewport, std::move(dispatcher), std::move(create),
                                   limits, std::move(memory), std::move(destinations)))
{
}

/// @brief Make retained navigation handlers inert without invoking lifecycle callbacks.
virtual_list::~virtual_list()
{
    if (impl_->owner != std::this_thread::get_id() || impl_->busy)
    {
        std::terminate();
    }

    impl_->navigation->active = false;
}

/// @brief Guard owner-thread staging and profile complete joined realization latency.
void virtual_list::update(std::shared_ptr<const item_source> source, float viewport_height,
                          const jobs::job_options& options)
{
    impl_->check();
    if (!source || !std::isfinite(viewport_height) || viewport_height < 0 ||
        !std::isfinite(source->extent()) || source->extent() < 0)
    {
        throw std::invalid_argument("invalid virtual source or viewport height");
    }

    const detail::virtual_guard guard{impl_->busy};
    const diagnostics::operation_scope scope{impl_->destinations, {"ui.virtual.update", "ui.cpu"}};
    impl_->publish(std::move(source), viewport_height, options);
}

/// @brief Queue a logical key for realization and reveal at the next boundary.
void virtual_list::request_focus(item_key key)
{
    impl_->check();
    if (!key)
    {
        throw std::invalid_argument("logical focus key cannot be zero");
    }

    impl_->navigation->pending = key;
}

/// @brief Restore physical focus only after the host captures current realized geometry.
void virtual_list::restore_focus()
{
    impl_->check();
    if (!impl_->navigation->focused)
    {
        return;
    }

    auto target = impl_->viewport;
    const auto current_focus = impl_->input.focused();
    for (const auto& row : impl_->rows)
    {
        if (row.item.key == impl_->navigation->focused)
        {
            if (row.item.offset >= impl_->previous_offset + impl_->previous_height ||
                row.item.offset + row.item.height <= impl_->previous_offset)
            {
                break;
            }

            for (auto current = current_focus; impl_->tree.contains(current);
                 current = impl_->tree.parent(current))
            {
                if (current == row.ticket.root)
                {
                    return;
                }
            }

            target = row.ticket.root;
            break;
        }
    }

    if (impl_->input.can_focus(target) && impl_->input.focused() != target)
    {
        const detail::virtual_guard guard{impl_->navigation->restoring};
        impl_->input.focus(target);
    }
}

/// @brief Observe logical focus independently of current physical realization.
item_key virtual_list::focused_item() const
{
    impl_->check();
    return impl_->navigation->focused;
}

/// @brief Reject ambiguous action IDs before replacing logical navigation bindings.
void virtual_list::configure_navigation(scroll_navigation_actions actions)
{
    impl_->check();
    const std::array values{actions.previous, actions.next, actions.first, actions.last};
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        for (std::size_t j = i + 1; j < values.size(); ++j)
        {
            if (values[i] && values[i] == values[j])
            {
                throw std::invalid_argument("duplicate virtual navigation action");
            }
        }
    }

    impl_->navigation->actions = actions;
}

/// @brief Reject late results from removed, rebound or failed binding attempts.
bool virtual_list::accepts(row_ticket ticket) const
{
    impl_->check();
    return impl_->tree.contains(ticket.root) && std::any_of(impl_->rows.begin(), impl_->rows.end(),
                                                            [ticket](const auto& row)
                                                            {
                                                                return row.ticket == ticket;
                                                            });
}

/// @brief Borrow logical-order row records until the next successful publication.
std::span<const realized_row> virtual_list::rows() const
{
    impl_->check();
    return impl_->rows;
}

/// @brief Read the last successful bounded publication accounting.
virtual_list_statistics virtual_list::statistics() const
{
    impl_->check();
    return impl_->stats;
}

/// @brief Atomically remove owned rows and retire their interaction registrations.
void virtual_list::detach()
{
    impl_->check();
    detail::input_edit bindings{impl_->input};
    detail::tree_edit edit{impl_->tree};
    for (const auto& lease : impl_->leases)
    {
        for (auto target : lease->bindings)
        {
            bindings.retire(target);
        }
    }

    if (impl_->content.generation)
    {
        edit.draft().remove(edit.staged(impl_->content));
        bindings.retire(impl_->viewport);
    }

    edit.commit();
    bindings.commit();
    impl_->content = {};
    impl_->rows.clear();
    impl_->leases.clear();
    impl_->navigation->source.reset();
    impl_->navigation->focused = 0;
    impl_->navigation->pending.reset();
}
} // namespace atlas::ui
