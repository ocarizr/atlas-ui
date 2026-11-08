/** @file @brief Transactional domain instance creation, reload and lifecycle retirement. */
#include "domain_state.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace atlas::scripting
{
/// @brief Append a logical context while sharing the domain's single physical interpreter.
instance_handle domain::create(behavior_spec spec)
{
    impl_->check();
    const auto context = impl_->contexts.find(spec.context);
    const auto source = impl_->sources->find(spec.source);
    if (context == impl_->contexts.end() || source == impl_->sources->end() ||
        impl_->entries.size() >= 1024 ||
        impl_->sequence == std::numeric_limits<std::uint64_t>::max())
    {
        throw std::invalid_argument("unknown source/context or exhausted instance capacity");
    }

    const detail::busy_guard guard{impl_->busy};
    auto behavior = std::make_shared<session>(context->second, impl_->limits, impl_->memory,
                                              impl_->destinations);
    if (!impl_->interpreter)
    {
        impl_->interpreter = std::make_shared<detail::lua_host>(impl_->limits, impl_->memory);
    }

    behavior->impl_->interpreter = impl_->interpreter;
    behavior->impl_->bindings = impl_->snapshot();
    behavior->load(source->second->text, source->second->source_name);
    if (impl_->frame)
    {
        behavior->begin_frame(impl_->frame);
    }

    const instance_handle handle{impl_->identity, ++impl_->sequence, 1};
    impl_->entries.push_back({handle, std::move(spec), std::move(behavior)});
    return handle;
}

/// @brief Publish a new generation only after its source has loaded successfully.
instance_handle domain::reload(instance_handle instance)
{
    impl_->check();
    auto& entry = impl_->get(instance);
    if (entry.identity.generation == std::numeric_limits<std::uint64_t>::max())
    {
        throw std::overflow_error("instance generations exhausted");
    }

    const detail::busy_guard guard{impl_->busy};
    const auto source = impl_->sources->at(entry.spec.source);
    auto candidate = std::make_shared<session>(entry.behavior->impl_->model, impl_->limits,
                                               impl_->memory, impl_->destinations);
    candidate->impl_->interpreter = impl_->interpreter;
    candidate->impl_->bindings = impl_->snapshot();
    candidate->impl_->sequence = entry.behavior->impl_->sequence;
    candidate->load(source->text, source->source_name);
    if (impl_->frame && entry.enabled)
    {
        candidate->begin_frame(impl_->frame);
    }

    entry.behavior->shutdown();
    entry.behavior = std::move(candidate);
    ++entry.identity.generation;
    return entry.identity;
}

/// @brief Removing an instance retires its subscriptions, native work and Lua registry roots.
void domain::remove(instance_handle instance)
{
    impl_->check();
    auto& entry = impl_->get(instance);
    const detail::busy_guard guard{impl_->busy};
    entry.behavior->shutdown();
    std::erase_if(impl_->entries,
                  [&](const auto& candidate)
                  {
                      return candidate.identity == instance;
                  });
}

/// @brief Disable preserves local script state but retires all external callbacks and work.
void domain::set_enabled(instance_handle instance, bool enabled)
{
    impl_->check();
    auto& entry = impl_->get(instance);
    if (entry.enabled == enabled)
    {
        return;
    }

    const detail::busy_guard guard{impl_->busy};
    if (!enabled)
    {
        entry.behavior->impl_->detach();
        entry.behavior->impl_->mail->close();
    }
    else
    {
        if (entry.behavior->inspect().failed)
        {
            throw std::logic_error("failed behavior requires reload before enable");
        }

        auto mail = std::make_shared<detail::mailbox>();

        mail->max_text = impl_->limits.max_text_bytes;
        mail->limit = impl_->limits.max_pending_bytes;
        entry.behavior->impl_->mail = std::move(mail);
        if (impl_->frame && entry.behavior->inspect().frame < impl_->frame)
        {
            entry.behavior->begin_frame(impl_->frame);
        }
    }

    entry.enabled = enabled;
}
} // namespace atlas::scripting
