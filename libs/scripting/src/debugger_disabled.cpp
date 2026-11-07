/** @file @brief Shipping/normal builds retain no debugger implementation or transport activity. */
#include "session_state.hpp"
#include <stdexcept>

namespace atlas::scripting::detail
{
/// @brief Debug attachment requires an explicit development build configuration.
void lua_vm::attach(debugger_options&&)
{
    throw std::logic_error("Lua debugger support disabled");
}

/// @brief Normal sessions have no debug resources.
void lua_vm::detach() noexcept
{
}

/// @brief Normal timing names remain unchanged.
bool lua_vm::debugging() const noexcept
{
    return false;
}

/// @brief Only the owned instruction quota needs a hook in production.
int lua_vm::hook_mask() const noexcept
{
    return LUA_MASKCOUNT;
}

/// @brief No debug callback executes in a normal build.
void lua_vm::debug_event(lua_Debug*)
{
}

/// @brief The protected selector cannot activate development code.
int lua_vm::initialize_debugger()
{
    return luaL_error(state, "Lua debugger support disabled");
}
} // namespace atlas::scripting::detail
