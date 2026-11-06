/** @file @brief Bind/retire checked UI targets without introducing model/UI ownership cycles. */
#include "binding_state.hpp"
#include <algorithm>
#include <atomic>
#include <limits>
#include <stdexcept>

namespace atlas::ui_mvvm
{
namespace
{
/// @brief Independent adapter lifetimes cannot accept each other's stale tokens.
std::uint64_t identity()
{
    static std::atomic<std::uint64_t> next{1};
    auto id = next.load();
    do
    {
        if (id == std::numeric_limits<std::uint64_t>::max())
        {
            throw std::overflow_error("UI MVVM identities exhausted");
        }
    } while (!next.compare_exchange_weak(id, id + 1));

    return id;
}
} // namespace

/// @brief Model subscriptions are weak toward adapter state; the model may outlive the adapter.
binding_session::impl::impl(ui::element_tree& target, std::shared_ptr<mvvm::view_model> source,
                            std::uint64_t writer, binding_limits policy,
                            std::shared_ptr<std::pmr::memory_resource> memory,
                            diagnostics::services services)
    : tree(target), model(std::move(source)), producer(writer), limits(policy),
      state(std::make_shared<binding_state>(std::move(memory))),
      listener(std::make_shared<dirty_observer>(state)), subscriptions(0, state->memory.get()),
      prepared(0, state->memory.get()), destinations(std::move(services)), session(identity())
{
    (void)model->description();
    (void)tree.contains({});
    state->bindings.reserve(limits.max_bindings);
    subscriptions.reserve(limits.max_bindings);
    prepared.reserve(limits.max_bindings);
}

/// @brief Reject foreign access before mutable guard observations.
void binding_session::impl::check() const
{
    if (owner != std::this_thread::get_id())
    {
        throw std::logic_error("UI MVVM requires its owner thread");
    }

    if (busy)
    {
        throw std::logic_error("UI MVVM is not reentrant");
    }
}

/// @brief A borrowed tree and retained independent model form the adapter composition boundary.
binding_session::binding_session(ui::element_tree& tree, std::shared_ptr<mvvm::view_model> model,
                                 std::uint64_t producer, binding_limits limits,
                                 std::shared_ptr<std::pmr::memory_resource> memory,
                                 diagnostics::services destinations)
{
    if (!model || !producer || !limits.max_bindings || limits.max_bindings > 65536)
    {
        throw std::invalid_argument("invalid UI MVVM configuration");
    }

    if (!memory)
    {
        memory = {std::pmr::new_delete_resource(), [](std::pmr::memory_resource*)
                  {
                  }};
    }

    impl_ = std::make_unique<impl>(tree, std::move(model), producer, limits, std::move(memory),
                                   std::move(destinations));
}

/// @brief Cleanup is owner-only and does not deliver UI or model callbacks.
binding_session::~binding_session()
{
    if (impl_->owner != std::this_thread::get_id() || impl_->busy)
    {
        std::terminate();
    }

    impl_->busy = true;
    for (const auto& source : impl_->subscriptions)
    {
        impl_->model->unsubscribe(source.token);
    }
}

/// @brief Compile the field name once, then store only handles and scalar policy.
binding_handle binding_session::bind(ui::element_handle target,
                                     const binding_definition& definition)
{
    impl_->check();
    const operation_guard guard{impl_->busy};
    if (!impl_->tree.contains(target) || definition.mode > binding_mode::two_way)
    {
        throw std::invalid_argument("invalid UI MVVM binding target/mode");
    }

    auto& bindings = impl_->state->bindings;
    if (std::any_of(bindings.begin(), bindings.end(),
                    [&](const auto& b)
                    {
                        return b.active && b.target == target && b.property == definition.target;
                    }))
    {
        throw std::invalid_argument("duplicate UI MVVM target property");
    }

    const auto vacant = std::find_if(bindings.begin(), bindings.end(),
                                     [](const auto& b)
                                     {
                                         return !b.active;
                                     });
    if (vacant == bindings.end() && bindings.size() == impl_->limits.max_bindings)
    {
        throw std::overflow_error("UI MVVM binding capacity exhausted");
    }

    if (impl_->sequence == std::numeric_limits<std::uint64_t>::max())
    {
        throw std::overflow_error("UI MVVM token identities exhausted");
    }

    const auto field = impl_->model->description().field(definition.source);
    (void)to_ui(*impl_->model, field, definition.target);
    auto subscription = std::find_if(impl_->subscriptions.begin(), impl_->subscriptions.end(),
                                     [&](const auto& s)
                                     {
                                         return s.field == field;
                                     });
    if (subscription == impl_->subscriptions.end())
    {
        const auto token = impl_->model->subscribe(field, impl_->listener);
        impl_->subscriptions.push_back({field, token, 1});
    }
    else
    {
        ++subscription->references;
    }

    binding_record record{target, definition.target, field, definition.mode, ++impl_->sequence};
    if (vacant == bindings.end())
    {
        bindings.push_back(record);
    }
    else
    {
        *vacant = record;
    }

    return {impl_->session, record.sequence};
}

/// @brief Only full session/sequence matches resolve; a retired token resolves to no binding.
binding_record* binding_session::impl::find(binding_handle token)
{
    if (token.session != session || !token.sequence)
    {
        throw std::invalid_argument("foreign UI MVVM binding token");
    }

    const auto it = std::find_if(state->bindings.begin(), state->bindings.end(),
                                 [&](const auto& b)
                                 {
                                     return b.active && b.sequence == token.sequence;
                                 });
    return it == state->bindings.end() ? nullptr : &*it;
}

/// @brief Last-consumer retirement removes the shared field observer and reclaims its metadata
/// slot.
void binding_session::impl::retire(binding_record& record)
{
    const auto it = std::find_if(subscriptions.begin(), subscriptions.end(),
                                 [&](const auto& s)
                                 {
                                     return s.field == record.field;
                                 });
    if (--it->references == 0)
    {
        model->unsubscribe(it->token);
        subscriptions.erase(it);
    }

    record.active = false;
    record.dirty = false;
}

/// @brief Repeated retirement is harmless while a foreign token is diagnosed.
void binding_session::unbind(binding_handle token)
{
    impl_->check();
    const operation_guard guard{impl_->busy};
    if (auto* record = impl_->find(token))
    {
        impl_->retire(*record);
    }
}

/// @brief Reverse writes are queued for the next explicit MVVM flush boundary.
void binding_session::write_back(binding_handle token, ui::property_value value)
{
    impl_->check();
    const operation_guard guard{impl_->busy};
    const auto* record = impl_->find(token);
    if (!record || record->mode != binding_mode::two_way || !impl_->tree.contains(record->target))
    {
        throw std::invalid_argument("UI MVVM binding is not a live two-way binding");
    }

    impl_->model->set(
        record->field,
        to_model(value, impl_->model->description().type(record->field), record->property));
}
} // namespace atlas::ui_mvvm
