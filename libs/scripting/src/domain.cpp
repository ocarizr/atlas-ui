/** @file @brief Domain ownership and monotonic source/context registration. */
#include "domain_state.hpp"
#include <atomic>
#include <limits>
#include <stdexcept>

namespace atlas::scripting
{
/// @brief Never wrap domain identities even when domains are created on independent owners.
static std::uint64_t next_domain_identity()
{
    static std::atomic<std::uint64_t> next{1};
    auto value = next.load();
    do
    {
        if (value == std::numeric_limits<std::uint64_t>::max())
        {
            throw std::overflow_error("domain identities exhausted");
        }
    } while (!next.compare_exchange_weak(value, value + 1));

    return value;
}

/// @brief Retain composition-root infrastructure; session construction validates execution limits.
domain::domain(session_limits limits, std::shared_ptr<std::pmr::memory_resource> memory,
               diagnostics::services destinations)
    : impl_(std::make_unique<impl>())
{
    detail::validate_limits(limits);
    impl_->limits = limits;
    impl_->memory =
        memory
            ? std::move(memory)
            : std::shared_ptr<std::pmr::memory_resource>{std::pmr::new_delete_resource(), [](auto*)
                                                         {
                                                         }};
    impl_->destinations = std::move(destinations);
    impl_->identity = next_domain_identity();
}

/// @brief Destruction outside the coordinator/dispatch contract is a host programming error.
domain::~domain()
{
    try
    {
        shutdown();
    }
    catch (...)
    {
        std::terminate();
    }
}

/// @brief No domain structural operation may reenter the shared physical interpreter.
void domain::impl::check() const
{
    if (owner != std::this_thread::get_id() || busy || closed ||
        (interpreter && (interpreter->executing || interpreter->dispatching)))
    {
        throw std::logic_error("domain is foreign, busy or closed");
    }
}

/// @brief Resolve only live handles from the exact domain and reload generation.
domain::impl::entry& domain::impl::get(instance_handle handle)
{
    for (auto& entry : entries)
    {
        if (entry.identity == handle)
        {
            return entry;
        }
    }

    throw std::invalid_argument("stale or foreign behavior instance");
}

/// @brief Named model contexts retain their independent source-of-truth ownership.
void domain::bind_context(std::string name, std::shared_ptr<mvvm::view_model> model)
{
    impl_->check();
    mvvm::validate_text(name, 128);
    if (name.empty() || !model || impl_->contexts.size() >= 256 || impl_->contexts.contains(name))
    {
        throw std::invalid_argument("invalid or duplicate model context");
    }

    (void)model->description();
    impl_->contexts.emplace(std::move(name), std::move(model));
}

/// @brief Publishing bytes does not reload live instances or perform storage access.
void domain::register_source(source_asset source)
{
    impl_->check();
    mvvm::validate_text(source.identity, 128);
    mvvm::validate_text(source.source_name, 1024);
    const auto found = impl_->sources->find(source.identity);
    if (source.identity.empty() || !source.revision || source.source_name.empty() ||
        source.source_name.find_first_of("\r\n") != std::string::npos || source.text.empty() ||
        source.text.size() > impl_->limits.max_source_bytes ||
        (found != impl_->sources->end() && source.revision <= found->second->revision) ||
        (found == impl_->sources->end() && impl_->sources->size() >= 256))
    {
        throw std::invalid_argument("invalid or stale source revision");
    }

    auto retained = std::make_shared<const source_asset>(std::move(source));
    auto next = std::make_shared<impl::source_catalog>(*impl_->sources);
    next->insert_or_assign(retained->identity, retained);
    impl_->sources = std::move(next);
}

/// @brief Cancel all behavior instances without affecting any other domain.
void domain::shutdown()
{
    if (impl_->owner != std::this_thread::get_id() || impl_->busy)
    {
        throw std::logic_error("invalid domain shutdown owner/reentry");
    }

    if (impl_->closed)
    {
        return;
    }

    impl_->check();
    const detail::busy_guard guard{impl_->busy};
    for (auto& entry : impl_->entries)
    {
        entry.behavior->shutdown();
    }

    impl_->entries.clear();
    impl_->messages.clear();
    impl_->contexts.clear();
    impl_->sources.reset();
    impl_->services.reset();
    impl_->interpreter.reset();
    impl_->closed = true;
    diagnostics::write(impl_->destinations, diagnostics::log_level::debug, "scripting",
                       "closed Lua domain and retired behavior instances");
}

/// @brief Aggregate actual physical interpreter memory rather than summing per-instance aliases.
domain_statistics domain::inspect() const
{
    impl_->check();
    domain_statistics result{impl_->frame, impl_->entries.size()};
    for (const auto& entry : impl_->entries)
    {
        result.enabled += entry.enabled ? 1 : 0;
    }

    if (impl_->interpreter)
    {
        result.interpreters = 1;
        result.lua_bytes = impl_->interpreter->memory.used;
        result.lua_peak_bytes = impl_->interpreter->memory.peak;
    }

    return result;
}
} // namespace atlas::scripting
