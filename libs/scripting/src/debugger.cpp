/** @file @brief Protected private LuaPanda environment lifecycle. */
#include "debugger_state.hpp"
#include <stdexcept>

namespace atlas::scripting::detail
{
/// @brief Select only the instruction hook during normal application execution.
int lua_vm::hook_mask() const noexcept
{
    return LUA_MASKCOUNT | (debugger ? LUA_MASKLINE | LUA_MASKCALL | LUA_MASKRET : 0);
}

/// @brief Restore globals even after protected initialization fails.
void lua_vm::restore_debug_globals()
{
    if (debugger->globals != LUA_NOREF)
    {
        lua_rawgeti(state, LUA_REGISTRYINDEX, debugger->globals);
        lua_rawseti(state, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
    }

    if (debugger->loaded != LUA_NOREF)
    {
        lua_rawgeti(state, LUA_REGISTRYINDEX, debugger->loaded);
        lua_setfield(state, LUA_REGISTRYINDEX, LUA_LOADED_TABLE);
    }
}

/// @brief Attach only while quiescent and keep all debugger setup under the protected entry.
void lua_vm::attach(debugger_options&& options)
{
    if (debugger || !options.transport || !options.port || options.agent_source.empty() ||
        options.agent_source.size() > 524288)
    {
        throw std::invalid_argument("invalid debugger attachment");
    }

    debugger = std::make_shared<debug_state>();
    debugger->options = std::move(options);
    operation_ = operation::attach;
    privileged = true;
    try
    {
        protect();
        if (host_error)
        {
            std::rethrow_exception(host_error);
        }

        if (debugger->connection_failed)
        {
            throw std::runtime_error("Lua debugger listener unavailable on 127.0.0.1; start the "
                                     "selected VS Code debug configuration");
        }

        restore_debug_globals();
        privileged = false;
    }
    catch (...)
    {
        restore_debug_globals();
        privileged = false;
        detach();
        throw;
    }
}

/// @brief Drop capabilities and connection without running debugger teardown Lua.
void lua_vm::detach() noexcept
{
    if (!debugger)
    {
        return;
    }

    debugger->options.transport->close();
    for (const auto reference :
         {debugger->hook, debugger->globals, debugger->loaded, debugger->environment})
    {
        luaL_unref(state, LUA_REGISTRYINDEX, reference);
    }

    debugger.reset();
}

/// @brief Debug-attached traces include debugger interaction and are labeled separately.
bool lua_vm::debugging() const noexcept
{
    return static_cast<bool>(debugger);
}

/// @brief Initialize standard debugger capabilities in a separate registry/global environment.
int lua_vm::initialize_debugger()
{
    lua_rawgeti(state, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
    debugger->globals = luaL_ref(state, LUA_REGISTRYINDEX);
    lua_getfield(state, LUA_REGISTRYINDEX, LUA_LOADED_TABLE);
    debugger->loaded = luaL_ref(state, LUA_REGISTRYINDEX);
    lua_newtable(state);
    lua_pushvalue(state, -1);
    debugger->environment = luaL_ref(state, LUA_REGISTRYINDEX);
    lua_rawseti(state, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
    lua_newtable(state);
    lua_setfield(state, LUA_REGISTRYINDEX, LUA_LOADED_TABLE);
    luaL_openselectedlibs(state,
                          LUA_GLIBK | LUA_TABLIBK | LUA_STRLIBK | LUA_MATHLIBK | LUA_UTF8LIBK |
                              LUA_DBLIBK | LUA_COLIBK | LUA_OSLIBK,
                          0);
    for (const auto* name : {"dofile", "loadfile"})
    {
        lua_pushnil(state);
        lua_setglobal(state, name);
    }

    lua_getglobal(state, "os");
    lua_getfield(state, -1, "time");
    lua_getfield(state, -2, "clock");
    lua_newtable(state);
    lua_pushvalue(state, -3);
    lua_setfield(state, -2, "time");
    lua_pushvalue(state, -2);
    lua_setfield(state, -2, "clock");
    lua_setglobal(state, "os");
    lua_pop(state, 3);
    lua_getglobal(state, "debug");
    lua_pushinteger(state, 0);
    lua_pushcclosure(state, debug_native, 1);
    lua_setfield(state, -2, "sethook");
    lua_pop(state, 1);
    for (const auto pair : {std::pair{"require", 6}, std::pair{"print", 7}})
    {
        lua_pushinteger(state, pair.second);
        lua_pushcclosure(state, debug_native, 1);
        lua_setglobal(state, pair.first);
    }

    lua_newtable(state);
    lua_pushliteral(state, "");
    lua_setfield(state, -2, "cpath");
    lua_setglobal(state, "package");
    lua_newtable(state);
    lua_pushinteger(state, 7);
    lua_pushcclosure(state, debug_native, 1);
    lua_setfield(state, -2, "open");
    lua_setglobal(state, "io");
    if (luaL_loadbufferx(state, debugger->options.agent_source.data(),
                         debugger->options.agent_source.size(), "@atlas-debugger/LuaPanda.lua",
                         "t") != LUA_OK)
    {
        return lua_error(state);
    }

    lua_call(state, 0, 1);
    lua_getfield(state, -1, "start");
    lua_pushliteral(state, "127.0.0.1");
    lua_pushinteger(state, debugger->options.port);
    lua_call(state, 2, 0);
    return 0;
}

} // namespace atlas::scripting::detail
