/** @file @brief Iterative state publication and bounded serial observer dispatch. */
#include "runtime_state.hpp"

namespace atlas::mvvm
{
/// @brief Equality reads typed columns directly; text is compared without making an owned copy.
bool view_model::impl::matches(std::uint32_t field, const value& data) const
{
    const auto index = layout->offsets[field];
    switch (type_of(data))
    {
        case value_type::boolean:
            return (booleans[index] != 0) == std::get<bool>(data);
        case value_type::integer:
            return integers[index] == std::get<std::int64_t>(data);
        case value_type::number:
            return numbers[index] == std::get<double>(data);
        case value_type::text:
            return std::string_view{texts[index]} == std::get<std::string>(data);
    }

    return false;
}

/// @brief Column layout is fixed at schema compilation; no runtime field-name dispatch occurs.
void view_model::impl::commit(std::uint32_t field, const value& data)
{
    const auto index = layout->offsets[field];
    switch (type_of(data))
    {
        case value_type::boolean:
            booleans[index] = std::get<bool>(data) ? 1 : 0;
            break;
        case value_type::integer:
            integers[index] = std::get<std::int64_t>(data);
            break;
        case value_type::number:
            numbers[index] = std::get<double>(data);
            break;
        case value_type::text:
            texts[index] = std::get<std::string>(data);
            break;
    }
}

/// @brief Retain the current observer through self-unsubscription and callback exceptions.
void view_model::impl::deliver(const queued_command& command)
{
    std::exception_ptr first;
    const auto none = std::numeric_limits<std::size_t>::max();
    for (auto i = buckets[bucket(command.kind, command.index)].head; i != none;
         i = listeners[i].next)
    {
        if (!listeners[i].listener)
        {
            continue;
        }

        if (stats.callbacks == limits.max_callbacks)
        {
            if (first)
            {
                std::rethrow_exception(first);
            }

            throw std::overflow_error("MVVM callback/cycle budget exhausted");
        }

        auto listener = listeners[i].listener;
        ++stats.callbacks;
        const diagnostics::operation_scope scope{destinations, {"mvvm.callback", "mvvm.dispatch"}};
        try
        {
            listener->on_notify({command.kind, command.index,
                                 std::span{arguments}.subspan(command.offset, command.count),
                                 stats.frame},
                                *facade);
        }
        catch (...)
        {
            if (!first)
            {
                first = std::current_exception();
            }
        }
    }

    if (first)
    {
        std::rethrow_exception(first);
    }
}

/// @brief Callback demands append rather than recursively entering dispatch or crossing a frame.
void view_model::flush()
{
    impl_->writable();
    if (impl_->flushing || impl_->admitting)
    {
        throw std::logic_error("recursive MVVM flush");
    }

    const detail::flag_guard guard{impl_->flushing};
    const diagnostics::operation_scope scope{impl_->destinations, {"mvvm.flush", "mvvm.cpu"}};
    ++impl_->segment;
    try
    {
        for (std::size_t i = 0; i < impl_->queue.size(); ++i)
        {
            const auto command = impl_->queue[i];
            ++impl_->segment;
            if (command.kind == notification_kind::changed)
            {
                auto& data = impl_->arguments[command.offset];
                impl_->resolve(command, data);
                if (impl_->matches(command.index, data))
                {
                    continue;
                }

                impl_->commit(command.index, data);
            }

            impl_->deliver(command);
        }

        impl_->queue.clear();
        impl_->arguments.clear();
        ++impl_->segment;
    }
    catch (...)
    {
        impl_->stats.failed = true;
        impl_->queue.clear();
        impl_->arguments.clear();
        ++impl_->segment;
        diagnostics::write(impl_->destinations, diagnostics::log_level::warning, "mvvm",
                           "dispatch failed; explicit reset required; committed values retained");
        throw;
    }
}
} // namespace atlas::mvvm
