/** @file @brief Atomic namespaced registry publication and immutable Lua source generations. */
#include "domain_state.hpp"
#include <algorithm>
#include <limits>

namespace atlas::scripting
{
namespace
{
/// @brief Registration is infrequent; copy candidates before publishing stable entry slots.
void insert(std::vector<detail::callable_entry>& entries, detail::callable_entry candidate)
{
    std::size_t bytes = detail::callable_metadata_bytes(candidate.descriptor);
    for (const auto& entry : entries)
    {
        if (entry.descriptor.id != candidate.descriptor.id)
        {
            bytes += detail::callable_metadata_bytes(entry.descriptor);
        }
    }

    if (bytes > std::size_t{8} * 1024 * 1024)
    {
        throw std::length_error("Callable registry metadata budget exhausted");
    }

    const auto found = std::ranges::find(entries, candidate.descriptor.id,
                                         [](const auto& item) -> const std::string&
                                         {
                                             return item.descriptor.id;
                                         });
    if (found != entries.end())
    {
        if (found->active)
        {
            throw std::invalid_argument("Callable ID already registered");
        }

        if (found->generation == std::numeric_limits<std::uint64_t>::max())
        {
            throw std::overflow_error("Callable generations exhausted");
        }

        candidate.generation = found->generation + 1;
        *found = std::move(candidate);
    }
    else
    {
        if (entries.size() == 512)
        {
            throw std::length_error("Callable registry capacity exhausted");
        }

        entries.push_back(std::move(candidate));
    }
}
} // namespace

/// @brief Native-only registrations allocate no Lua interpreter or script execution context.
void domain::register_action(callable_origin origin, callable_descriptor descriptor,
                             native_action action)
{
    impl_->check();
    descriptor.kind = callable_kind::action;
    detail::validate_callable(origin, descriptor);
    if (!action)
    {
        throw std::invalid_argument("Native action requires an implementation");
    }

    auto next = impl_->callables.entries;
    insert(next, {std::move(descriptor), std::move(origin), std::move(action)});
    impl_->callables.entries.swap(next);
}

/// @brief A predicate receives no model mutation or native-work capability.
void domain::register_condition(callable_origin origin, callable_descriptor descriptor,
                                native_condition condition)
{
    impl_->check();
    descriptor.kind = callable_kind::condition;
    detail::validate_callable(origin, descriptor);
    if (!condition)
    {
        throw std::invalid_argument("Native condition requires an implementation");
    }

    auto next = impl_->callables.entries;
    insert(next, {std::move(descriptor), std::move(origin), {}, std::move(condition)});
    impl_->callables.entries.swap(next);
}

/// @brief Discovery gets isolated globals on the same physical owner interpreter.
void domain::load_callable_module(callable_module spec)
{
    impl_->check();
    const auto source = impl_->sources->find(spec.source);
    if (source == impl_->sources->end())
    {
        throw std::invalid_argument("Unknown callable entry source");
    }

    const detail::busy_guard guard{impl_->busy};
    const diagnostics::operation_scope scope{impl_->destinations,
                                             {"script.registry.load", "script.cpu"}};
    auto module = std::make_shared<detail::callable_module_state>();
    module->spec = std::move(spec);
    module->text = source->second->text;
    module->source_name = source->second->source_name;
    module->sources = impl_->snapshot();
    if (!impl_->interpreter)
    {
        impl_->interpreter = std::make_shared<detail::lua_host>(impl_->limits, impl_->memory);
    }

    auto dummy = std::make_shared<mvvm::view_model>(mvvm::compile({"callable_discovery"}));
    auto discovery =
        detail::load_callable_vm(dummy, *module, impl_->limits, impl_->interpreter, impl_->memory);
    module->definitions = discovery->bindings->registry->definitions;
    if (module->definitions.empty())
    {
        throw std::invalid_argument("Callable module registered no functions");
    }

    std::shared_ptr<detail::callable_module_state> previous;
    auto next = impl_->callables.entries;
    for (auto& entry : next)
    {
        if (entry.active && entry.module && entry.module->spec.origin == module->spec.origin)
        {
            if (entry.module->spec.source != module->spec.source)
            {
                throw std::invalid_argument("Contributor namespace already has another entrypoint");
            }

            previous = entry.module;
            entry.active = false;
            entry.module.reset();
        }
    }

    for (const auto& definition : module->definitions)
    {
        insert(next, {definition, module->spec.origin, {}, {}, module});
    }

    impl_->callables.entries.swap(next);
    if (previous)
    {
        impl_->retire_callables(previous);
    }
}

/// @brief Retirement cancels weak worker destinations before releasing private Lua module state.
void domain::impl::retire_callables(const std::shared_ptr<detail::callable_module_state>& module)
{
    std::erase_if(callables.contexts,
                  [&](const auto& item)
                  {
                      if (item.second->module != module)
                      {
                          return false;
                      }

                      item.second->mail->close();
                      return true;
                  });
}

/// @brief Names are inspected on demand; execution never scans source code or descriptor tables.
std::vector<callable_descriptor> domain::callable_descriptors() const
{
    impl_->check();
    std::vector<callable_descriptor> result;
    result.reserve(impl_->callables.entries.size());
    for (const auto& entry : impl_->callables.entries)
    {
        if (entry.active)
        {
            result.push_back(entry.descriptor);
        }
    }

    return result;
}

/// @brief Removing an entry invalidates both issued work and old bound handles.
void domain::remove_callable(std::string_view id)
{
    impl_->check();
    const auto found = std::ranges::find(impl_->callables.entries, id,
                                         [](const auto& item) -> std::string_view
                                         {
                                             return item.descriptor.id;
                                         });
    if (found == impl_->callables.entries.end() || !found->active)
    {
        throw std::invalid_argument("Unknown callable ID");
    }

    const auto index = static_cast<std::size_t>(found - impl_->callables.entries.begin()) + 1;
    std::erase_if(impl_->callables.contexts,
                  [&](const auto& item)
                  {
                      const bool owns = found->module ? item.second->module == found->module
                                                      : item.second->native_entry == index;
                      if (!owns)
                      {
                          return false;
                      }

                      item.second->mail->close();
                      return true;
                  });
    found->active = false;
}
} // namespace atlas::scripting
