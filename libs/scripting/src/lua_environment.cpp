/** @file @brief Restricted per-instance libraries keep mutable globals and string methods isolated.
 */
#include "session_state.hpp"

namespace atlas::scripting::detail
{
/// @brief Open private standard libraries without filesystem, protected-call or shared-table
/// escapes.
void lua_vm::open_restricted_libraries()
{
    luaL_openselectedlibs(state,
                          LUA_GLIBK | LUA_TABLIBK | LUA_STRLIBK | LUA_MATHLIBK | LUA_UTF8LIBK, 0);
    for (const auto* name : {"dofile", "loadfile", "load", "print", "warn", "collectgarbage",
                             "pcall", "xpcall", "setmetatable", "getmetatable", "pairs", "next"})
    {
        lua_pushnil(state);
        lua_setglobal(state, name);
    }

    lua_getglobal(state, "math");
    for (const auto* name : {"random", "randomseed"})
    {
        lua_pushnil(state);
        lua_setfield(state, -2, name);
    }

    lua_pop(state, 1);
    // The string metatable retains a pristine library, never the mutable public table.
    lua_getglobal(state, "string");
    lua_newtable(state);
    lua_pushnil(state);
    while (lua_next(state, -3))
    {
        lua_pushvalue(state, -2);
        lua_pushvalue(state, -2);
        lua_rawset(state, -5);
        lua_pop(state, 1);
    }

    lua_setglobal(state, "string");
    lua_pop(state, 1);
}
} // namespace atlas::scripting::detail
