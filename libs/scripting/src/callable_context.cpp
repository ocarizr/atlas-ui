/** @file @brief Borrowed read capabilities and transactional model/native-work producers. */
#include "callable_state.hpp"

namespace atlas::scripting::detail
{
/// @brief Reject forbidden side effects before staging or dispatching anything.
void call_scope::demand()
{
    if (read_only)
    {
        throw std::logic_error("Navigation conditions are read-only");
    }

    if (commands.size() + requests.size() >= limits.max_commands)
    {
        throw std::overflow_error("Callable command budget exhausted");
    }
}
} // namespace atlas::scripting::detail

namespace atlas::scripting
{
/// @brief Only an active owner invocation may lend this capability.
condition_context::condition_context(detail::call_scope& scope) : scope_{scope}
{
}

/// @brief Native and Lua producers share the same scoped demand storage.
action_context::action_context(detail::call_scope& scope) : condition_context{scope}
{
}

/// @brief Read committed values; staged writes are intentionally not visible during dispatch.
mvvm::value condition_context::get(std::string_view field) const
{
    return scope_.model.read(scope_.model.description().field(field));
}

/// @brief Queue only owned scalar data; model submission performs complete schema preflight.
void action_context::set(std::string_view field, mvvm::value value)
{
    scope_.demand();
    detail::validate_values(std::span{&value, 1}, scope_.limits.max_text_bytes);
    scope_.commands.push_back(
        {scope_.model.description().field(field), mvvm::update_kind::assign, {std::move(value)}});
}

/// @brief A model action executes at the later ViewModel synchronization phase.
void action_context::invoke(std::string_view action, std::span<const mvvm::value> arguments)
{
    scope_.demand();
    detail::validate_values(arguments, scope_.limits.max_text_bytes);
    scope_.commands.push_back({scope_.model.description().action(action),
                               mvvm::update_kind::assign,
                               {arguments.begin(), arguments.end()}});
}

/// @brief Event listeners are not invoked from the producer callback.
void action_context::emit(std::string_view event, std::span<const mvvm::value> arguments)
{
    scope_.demand();
    detail::validate_values(arguments, scope_.limits.max_text_bytes);
    scope_.commands.push_back({scope_.model.description().event(event),
                               mvvm::update_kind::assign,
                               {arguments.begin(), arguments.end()}});
}

/// @brief Heavy work is a value-only request, never a blocking operation in the callback.
void action_context::request(std::string_view operation, std::span<const mvvm::value> arguments)
{
    scope_.demand();
    mvvm::validate_text(operation, 128);
    if (operation.empty())
    {
        throw std::invalid_argument("Callable work requires an operation");
    }

    detail::validate_values(arguments, scope_.limits.max_text_bytes);
    scope_.requests.push_back(
        {0, std::string{operation}, {arguments.begin(), arguments.end()}, {}});
}
} // namespace atlas::scripting
