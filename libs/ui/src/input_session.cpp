/** @file @brief Checked ownership, sparse handler admission and observations. */
#include "input_state.hpp"
#include <tuple>

namespace atlas::ui
{
/// @brief No tree pointer or callback is copied to a worker.
input_session::impl::impl(element_tree& source, input_limits policy,
                          std::shared_ptr<std::pmr::memory_resource> resource,
                          diagnostics::services services)
    : tree(source), limits(policy), memory(std::move(resource)), destinations(std::move(services)),
      bindings(0, memory.get()), regions(0, memory.get()), slots(0, memory.get()),
      path(0, memory.get()), session(tree.snapshot().session())
{
    bindings.reserve(limits.max_bindings);
    path.reserve(limits.max_route_depth);
}

/// @brief Affinity is checked before busy or other mutable fields.
void input_session::impl::check() const
{
    jobs::require_coordinator_thread();
    if (owner != std::this_thread::get_id() || busy)
    {
        throw std::logic_error("UI input requires its non-reentrant owner");
    }
}

/// @brief Registration changes are boundary operations, never callback mutation.
void input_session::impl::idle() const
{
    check();
    if (terminal || (!stats.closed && !stats.failed))
    {
        throw std::logic_error("configure UI input between successful frames");
    }
}

/// @brief A failed frame can only recover through a newer begin or terminal shutdown.
void input_session::impl::open() const
{
    check();
    if (terminal || stats.closed || stats.failed)
    {
        throw std::logic_error("UI input requires an open successful frame");
    }
}

/// @brief Sorted full identities distinguish immediate slot reuse from old handler cleanup.
const input_binding* input_session::impl::find(element_handle target) const
{
    const auto it =
        std::lower_bound(bindings.begin(), bindings.end(), target,
                         [](const input_binding& value, element_handle key)
                         {
                             return std::tie(value.target.index, value.target.generation) <
                                    std::tie(key.index, key.generation);
                         });
    return it != bindings.end() && it->target == target ? &*it : nullptr;
}

/// @brief Const lookup does not mutate registration state.
input_binding* input_session::impl::find(element_handle target)
{
    return const_cast<input_binding*>(std::as_const(*this).find(target));
}

/// @brief Validate limits before reserving metadata or borrowing a tree session.
input_session::input_session(element_tree& tree, input_limits limits,
                             std::shared_ptr<std::pmr::memory_resource> memory,
                             diagnostics::services destinations)
{
    jobs::require_coordinator_thread();
    if (!limits.max_nodes || limits.max_nodes > 65536 || !limits.max_bindings ||
        limits.max_bindings > 65536 || !limits.max_events || limits.max_events > 4096 ||
        !limits.max_route_depth || limits.max_route_depth > 65536 || !limits.max_callbacks ||
        limits.max_callbacks > 1048576)
    {
        throw std::invalid_argument("invalid UI input limits");
    }

    if (!memory)
    {
        memory = {std::pmr::new_delete_resource(), [](std::pmr::memory_resource*)
                  {
                  }};
    }

    impl_ = std::make_unique<impl>(tree, limits, std::move(memory), std::move(destinations));
}

/// @brief Destruction releases retained callbacks; orderly lifecycle uses explicit shutdown.
input_session::~input_session()
{
    if (impl_->owner != std::this_thread::get_id() || impl_->busy)
    {
        std::terminate();
    }

    impl_->busy = true;
}

/// @brief A new registration cannot overwrite a pending cleanup generation.
void input_session::bind(element_handle target, std::shared_ptr<input_handler> handler,
                         interaction_options options)
{
    impl_->idle();
    const impl::guard guard{impl_->busy};
    std::erase_if(impl_->bindings,
                  [](const auto& value)
                  {
                      return value.ended;
                  });
    if (!handler || !impl_->tree.contains(target) || impl_->find(target))
    {
        throw std::invalid_argument("UI input binding needs a live unique target and handler");
    }

    if (impl_->bindings.size() == impl_->limits.max_bindings)
    {
        throw std::overflow_error("UI input binding limit exhausted");
    }

    impl_->bindings.push_back({target, std::move(handler), options});
    std::sort(impl_->bindings.begin(), impl_->bindings.end(),
              [](const auto& a, const auto& b)
              {
                  return std::tie(a.target.index, a.target.generation) <
                         std::tie(b.target.index, b.target.generation);
              });
}

/// @brief Changes become visible at the following admitted frame boundary.
void input_session::configure(element_handle target, interaction_options options)
{
    impl_->idle();
    auto* value = impl_->find(target);
    if (!value || value->retiring || value->ended)
    {
        throw std::invalid_argument("unknown active UI input registration");
    }

    value->options = options;
}

/// @brief Handler ownership survives until the next serial disable/end attempt.
void input_session::unbind(element_handle target)
{
    impl_->idle();
    auto* value = impl_->find(target);
    if (!value || value->ended)
    {
        throw std::invalid_argument("unknown UI input registration");
    }

    value->retiring = true;
}

/// @brief No observer can bypass the callback reentrancy contract.
element_handle input_session::focused() const
{
    impl_->check();
    return impl_->focus_target;
}

/// @brief Observe primary-pointer capture on its owner.
element_handle input_session::captured() const
{
    impl_->check();
    return impl_->capture_target;
}

/// @brief Observe geometric hover independently of capture.
element_handle input_session::hovered() const
{
    impl_->check();
    return impl_->hover_target;
}

/// @brief Account callback attempts rather than claiming CPU or memory utilization.
input_statistics input_session::statistics() const
{
    impl_->check();
    return impl_->stats;
}
} // namespace atlas::ui
