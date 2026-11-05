/** @file @brief Immutable signature validation reused by runtime and data-authored adapters. */
#include "schema_state.hpp"

namespace atlas::mvvm
{
namespace
{
/// @brief Validate protocol payloads without admitting a frame command or invoking observers.
void validate(const signal_definition& signal, std::span<const value> args, std::size_t max_text)
{
    if (args.size() != signal.parameters.size())
    {
        throw std::invalid_argument("MVVM argument count mismatch for " + signal.name);
    }

    for (std::size_t i = 0; i < args.size(); ++i)
    {
        if (type_of(args[i]) != signal.parameters[i].type)
        {
            throw std::invalid_argument("MVVM argument type mismatch for " + signal.name + "." +
                                        signal.parameters[i].name);
        }

        detail::validate_value(args[i], max_text);
    }
}
} // namespace

/// @brief Adapters can validate an authored action before any mutable frame exists.
void schema::validate(action_handle slot, std::span<const value> arguments) const
{
    if (slot.schema != data_->id || slot.index >= data_->source.actions.size())
    {
        throw std::invalid_argument("foreign or invalid MVVM action slot");
    }

    mvvm::validate(data_->source.actions[slot.index], arguments, data_->limits.max_text_bytes);
}

/// @brief Output protocols share scalar validation but never share action slot identities.
void schema::validate(event_handle slot, std::span<const value> arguments) const
{
    if (slot.schema != data_->id || slot.index >= data_->source.events.size())
    {
        throw std::invalid_argument("foreign or invalid MVVM event slot");
    }

    mvvm::validate(data_->source.events[slot.index], arguments, data_->limits.max_text_bytes);
}
} // namespace atlas::mvvm
