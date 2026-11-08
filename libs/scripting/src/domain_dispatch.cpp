/** @file @brief Explicit coordinator phases route ordered domain events through the Lua director.
 */
#include "domain_state.hpp"
#include <stdexcept>

namespace atlas::scripting
{
/// @brief Preflight all enabled contexts before admitting the next shared logical frame.
void domain::begin_frame(std::uint64_t frame)
{
    impl_->check();
    if (impl_->message_head != impl_->messages.size())
    {
        throw std::logic_error("domain messages must finish in their admitted frame");
    }

    if (!frame || frame <= impl_->frame)
    {
        throw std::invalid_argument("domain frame must increase");
    }

    for (const auto& entry : impl_->entries)
    {
        if (!entry.enabled)
        {
            continue;
        }

        const auto model = entry.behavior->impl_->model->inspect();
        if (model.frame != frame || model.failed || entry.behavior->inspect().failed)
        {
            throw std::logic_error("domain requires healthy matching model frames");
        }
    }

    const detail::busy_guard guard{impl_->busy};
    for (auto& entry : impl_->entries)
    {
        if (entry.enabled)
        {
            entry.behavior->begin_frame(frame);
        }
    }

    impl_->frame = frame;
}

/// @brief Direct dispatch validates generation and activation before entering the director.
std::vector<mvvm::value> domain::call(instance_handle instance, std::string_view handler,
                                      std::span<const mvvm::value> arguments,
                                      std::span<const mvvm::value_type> result_types)
{
    impl_->check();
    auto& entry = impl_->get(instance);
    if (!entry.enabled)
    {
        throw std::logic_error("behavior instance is disabled");
    }

    const detail::busy_guard guard{impl_->busy};
    return entry.behavior->call(handler, arguments, result_types);
}

/// @brief Each event observes registration order; completed earlier calls are not rolled back.
void domain::dispatch(std::string_view handler, std::span<const mvvm::value> arguments)
{
    impl_->check();
    const detail::busy_guard guard{impl_->busy};
    const diagnostics::operation_scope scope{impl_->destinations,
                                             {"script.domain.dispatch", "script.dispatch"}};
    for (auto& entry : impl_->entries)
    {
        if (entry.enabled)
        {
            entry.behavior->call(handler, arguments);
        }
    }
}

/// @brief Transfer native requests without sharing interpreter state or extending its lifetime.
std::vector<instance_work> domain::take_requests()
{
    impl_->check();
    const detail::busy_guard guard{impl_->busy};
    std::vector<instance_work> result;
    for (auto& entry : impl_->entries)
    {
        if (!entry.enabled)
        {
            continue;
        }

        for (auto& work : entry.behavior->take_requests())
        {
            result.push_back({entry.identity, std::move(work)});
        }
    }

    return result;
}

/// @brief Domain disposal and instance generation retirement invalidate weak result destinations.
void domain::deliver_results()
{
    impl_->check();
    const detail::busy_guard guard{impl_->busy};
    for (auto& entry : impl_->entries)
    {
        if (entry.enabled)
        {
            entry.behavior->deliver_results();
        }
    }
}

/// @brief Inspect one exact live instance on its owner thread.
statistics domain::inspect(instance_handle instance) const
{
    impl_->check();
    return impl_->get(instance).behavior->inspect();
}

/// @brief Attachment is explicit per instance and retains the domain's physical stack owner.
void domain::attach_debugger(instance_handle instance, debugger_options options)
{
    impl_->check();
    const detail::busy_guard guard{impl_->busy};
    impl_->get(instance).behavior->attach_debugger(std::move(options));
}

/// @brief Drop only the selected instance's development connection.
void domain::detach_debugger(instance_handle instance)
{
    impl_->check();
    const detail::busy_guard guard{impl_->busy};
    impl_->get(instance).behavior->detach_debugger();
}
} // namespace atlas::scripting
