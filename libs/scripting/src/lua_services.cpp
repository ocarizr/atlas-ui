/** @file @brief Direct scalar services preserve errors without exposing interpreter objects. */
#include "session_state.hpp"
#include <stdexcept>

namespace atlas::scripting::detail
{
/// @brief Preserve the original project exception across protected Lua error conversion.
int lua_vm::service_native(lua_State* state)
{
    auto& self = **static_cast<lua_vm**>(lua_getextraspace(state));
    try
    {
        return self.call_service();
    }
    catch (const std::exception&)
    {
        self.host_error = std::current_exception();
    }

    return luaL_error(state, "synchronous service failed");
}

/// @brief Execution is bounded, synchronous and restricted to owned scalar protocols.
int lua_vm::call_service()
{
    if (loading || !bindings || !bindings->service || lua_gettop(state) < 1 ||
        lua_gettop(state) > 33 || lua_type(state, 1) != LUA_TSTRING ||
        ++service_calls_ > limits.max_commands)
    {
        throw std::invalid_argument("service needs a bounded admitted call and identity");
    }

    const auto name = std::get<std::string>(read(1));
    mvvm::validate_text(name, 128);
    std::vector<mvvm::value> arguments;
    arguments.reserve(static_cast<std::size_t>(lua_gettop(state) - 1));
    for (int i = 2; i <= lua_gettop(state); ++i)
    {
        arguments.push_back(read(i));
    }

    validate_values(arguments, limits.max_text_bytes);
    const auto result = bindings->service(name, arguments);
    validate_values(result, limits.max_text_bytes);
    for (const auto& value : result)
    {
        push(value);
    }

    return static_cast<int>(result.size());
}
} // namespace atlas::scripting::detail
