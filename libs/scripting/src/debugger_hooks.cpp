/** @file @brief Owned quota-compatible debugger event delivery. */
#include "debugger_state.hpp"

namespace atlas::scripting::detail
{
/// @brief Deliver only the hook events requested by LuaPanda while preserving quota counters.
void lua_vm::debug_event(lua_Debug* event)
{
    if (!debugger || debugger->hook == LUA_NOREF || privileged)
    {
        return;
    }

    const auto mask = event->event == LUA_HOOKTAILCALL ? LUA_MASKCALL : 1 << event->event;
    if (!(debugger->mask & mask))
    {
        return;
    }

    if (event->event == LUA_HOOKCOUNT)
    {
        debugger->elapsed += 1;
        if (debugger->elapsed < debugger->count)
        {
            return;
        }

        debugger->elapsed = 0;
    }

    const auto top = lua_gettop(state);
    const char* name = event->event == LUA_HOOKCALL       ? "call"
                       : event->event == LUA_HOOKRET      ? "return"
                       : event->event == LUA_HOOKLINE     ? "line"
                       : event->event == LUA_HOOKTAILCALL ? "tail call"
                                                          : "count";
    lua_rawgeti(state, LUA_REGISTRYINDEX, debugger->hook);
    lua_pushstring(state, name);
    if (event->event == LUA_HOOKLINE)
    {
        lua_pushinteger(state, event->currentline);
    }
    else
    {
        lua_pushnil(state);
    }

    lua_call(state, 2, 0);
    lua_settop(state, top);
}

} // namespace atlas::scripting::detail
