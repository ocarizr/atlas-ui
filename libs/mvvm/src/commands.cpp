/** @file @brief Checked bounded demand admission with segment-local field coalescing. */
#include "runtime_state.hpp"
#include <algorithm>

namespace atlas::mvvm
{
/// @brief Queue publication follows complete type/payload/capacity validation.
void view_model::impl::enqueue(notification_kind kind, std::uint32_t index,
                               std::span<const value> args, value* owned_field,
                               mutation_kind mutation)
{
    writable();
    const detail::flag_guard guard{admitting};
    if (stats.commands == limits.max_commands)
    {
        throw std::overflow_error("MVVM command/cycle budget exhausted");
    }

    std::size_t bytes = 0;
    for (const auto& arg : args)
    {
        if (const auto* text = std::get_if<std::string>(&arg))
        {
            bytes += text->size();
        }
    }

    if (bytes > limits.max_payload_bytes - payload_bytes)
    {
        throw std::overflow_error("MVVM payload budget exhausted");
    }

    if (kind == notification_kind::changed && mutation == mutation_kind::assign)
    {
        auto& pending = latest[index];
        if (pending.segment == segment)
        {
            auto& previous = arguments[queue[pending.command].offset];
            value replacement;
            if (owned_field)
            {
                replacement = std::move(*owned_field);
            }
            else
            {
                replacement = args.front();
            }

            const bool conflict = previous != replacement;
            previous = std::move(replacement);
            ++stats.commands;
            payload_bytes += bytes;
            if (conflict)
            {
                ++stats.conflicts;
                if (stats.conflicts <= limits.max_warning_details)
                {
                    diagnostics::write(destinations, diagnostics::log_level::warning, "mvvm",
                                       "conflicting pending field writes; last value selected");
                }
            }

            return;
        }
    }

    if (args.size() > limits.max_arguments - admitted_arguments)
    {
        throw std::overflow_error("MVVM argument budget exhausted");
    }

    const auto start = arguments.size();
    if (owned_field)
    {
        arguments.push_back(std::move(*owned_field));
    }
    else
    {
        try
        {
            staged.assign(args.begin(), args.end());
        }
        catch (...)
        {
            staged.clear();
            throw;
        }

        arguments.insert(arguments.end(), std::make_move_iterator(staged.begin()),
                         std::make_move_iterator(staged.end()));
        staged.clear();
    }

    queue.push_back({kind, index, start, args.size(), mutation});
    if (kind == notification_kind::changed && mutation == mutation_kind::assign)
    {
        latest[index] = {segment, queue.size() - 1};
    }
    else
    {
        ++segment;
    }

    ++stats.commands;
    admitted_arguments += args.size();
    payload_bytes += bytes;
}

/// @brief Scalar type checks happen before the pending queue is changed.
void view_model::set(field_handle slot, value data)
{
    impl_->writable();
    if (impl_->description.type(slot) != type_of(data))
    {
        throw std::invalid_argument("MVVM write type mismatch");
    }

    detail::validate_value(data, impl_->layout->limits.max_text_bytes);
    impl_->enqueue(notification_kind::changed, slot.index, std::span{&data, 1}, &data);
}

/// @brief Each invocation remains an ordered event, even when its payload repeats.
void view_model::invoke(action_handle slot, std::span<const value> args)
{
    impl_->writable();
    impl_->description.validate(slot, args);
    impl_->enqueue(notification_kind::action, slot.index, args);
}

/// @brief Explicit events use the same bounded queue but a separate subscription namespace.
void view_model::emit(event_handle slot, std::span<const value> args)
{
    impl_->writable();
    impl_->description.validate(slot, args);
    impl_->enqueue(notification_kind::event, slot.index, args);
}
} // namespace atlas::mvvm
