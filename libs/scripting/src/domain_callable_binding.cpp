/** @file @brief Lazy per-model module contexts and generation-checked bound invocation slots. */
#include "domain_state.hpp"
#include <algorithm>
#include <limits>

namespace atlas::scripting::detail
{
/// @brief Discovery and live contexts share code bytes, not mutable module globals.
std::unique_ptr<lua_vm> load_callable_vm(std::shared_ptr<mvvm::view_model> model,
                                         const callable_module_state& module, session_limits limits,
                                         const std::shared_ptr<lua_host>& host,
                                         std::shared_ptr<std::pmr::memory_resource> memory)
{
    auto bindings = std::make_shared<execution_bindings>();
    bindings->source = module.sources->source;
    bindings->registry = std::make_shared<callable_registration>();
    bindings->registry->origin = module.spec.origin;
    auto vm = std::make_unique<lua_vm>(std::move(model), limits, std::move(memory), host, bindings);
    vm->load(module.text, module.source_name);
    if (!module.definitions.empty() && bindings->registry->definitions != module.definitions)
    {
        throw std::invalid_argument("Callable module discovery schema changed during binding");
    }

    if (!module.definitions.empty())
    {
        bindings->registry->definitions.clear();
    }

    return vm;
}
} // namespace atlas::scripting::detail

namespace atlas::scripting
{
/// @brief Public handles never retain a domain, implementation pointer or private interpreter.
detail::callable_entry& domain::impl::callable(callable_binding binding)
{
    if (binding.domain != identity || !binding.sequence ||
        binding.sequence > callables.entries.size())
    {
        throw std::invalid_argument("Stale or foreign callable binding");
    }

    auto& entry = callables.entries.at(static_cast<std::size_t>(binding.sequence - 1));
    if (!entry.active || entry.generation != binding.generation)
    {
        throw std::invalid_argument("Stale callable registration generation");
    }

    return entry;
}

/// @brief Match both the entry generation and exact bound model context before any invocation.
detail::callable_context& domain::impl::callable_context(callable_binding binding)
{
    const auto& entry = callable(binding);
    const auto found = callables.contexts.find(binding.context);
    if (found == callables.contexts.end() || found->second->module != entry.module ||
        (!entry.module && found->second->native_entry != binding.sequence))
    {
        throw std::invalid_argument("Stale callable model context");
    }

    return *found->second;
}

/// @brief Module contexts are shared by their functions but isolated between bound models.
detail::callable_context& domain::impl::bind_callable_context(std::size_t index,
                                                              std::string_view name)
{
    auto& entry = callables.entries.at(index);
    const auto model = contexts.find(name);
    if (model == contexts.end())
    {
        throw std::invalid_argument("Unknown callable model context");
    }

    for (const auto& [id, context] : callables.contexts)
    {
        if (context->name == name && context->module == entry.module &&
            (entry.module || context->native_entry == index + 1))
        {
            return *context;
        }
    }

    if (callables.contexts.size() >= 128 ||
        callables.sequence == std::numeric_limits<std::uint64_t>::max())
    {
        throw std::length_error("Callable context capacity exhausted");
    }

    auto candidate = std::make_unique<detail::callable_context>();
    candidate->name = name;
    candidate->model = model->second;
    candidate->module = entry.module;
    candidate->native_entry = entry.module ? 0 : index + 1;
    candidate->mail->max_text = limits.max_text_bytes;
    candidate->mail->limit = limits.max_pending_bytes;
    if (entry.module)
    {
        candidate->vm =
            detail::load_callable_vm(model->second, *entry.module, limits, interpreter, memory);
    }

    const auto id = ++callables.sequence;
    return *callables.contexts.emplace(id, std::move(candidate)).first->second;
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters) -- Mirrors existing source/context binding
// order.
/// @brief Bind a stable registry ID to the explicitly selected model context.
callable_binding domain::bind_callable(std::string_view id, std::string_view context)
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

    const detail::busy_guard guard{impl_->busy};
    const auto index = static_cast<std::size_t>(found - impl_->callables.entries.begin());
    const auto& bound = impl_->bind_callable_context(index, context);
    for (const auto& [identity, candidate] : impl_->callables.contexts)
    {
        if (candidate.get() == &bound)
        {
            return {impl_->identity, index + 1, found->generation, identity};
        }
    }

    throw std::logic_error("Callable context was not retained");
}

// NOLINTEND(bugprone-easily-swappable-parameters)

/// @brief A liveness query is still owner-only, but stale/foreign tokens simply return false.
bool domain::callable_current(callable_binding binding) const
{
    impl_->check();
    try
    {
        (void)impl_->callable_context(binding);
        return true;
    }
    catch (const std::invalid_argument&)
    {
        return false;
    }
}
} // namespace atlas::scripting
