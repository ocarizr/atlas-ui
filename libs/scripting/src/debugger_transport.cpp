/** @file @brief Private LuaSocket-compatible development transport bridge. */
#include "debugger_state.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace atlas::scripting::detail
{
/// @brief Convert transport errors through the same native failure boundary as model bindings.
int lua_vm::debug_native(lua_State* state)
{
    auto& self = **static_cast<lua_vm**>(lua_getextraspace(state));
    try
    {
        return self.debug_operation(static_cast<int>(lua_tointeger(state, lua_upvalueindex(1))));
    }
    catch (const std::exception&)
    {
        self.host_error = std::current_exception();
    }

    return luaL_error(state, "debugger transport failed");
}

/// @brief Replace the agent subscription without changing the owned native quota hook.
int lua_vm::set_debug_hook()
{
    auto& debug = *debugger;
    luaL_unref(state, LUA_REGISTRYINDEX, debug.hook);
    debug.hook = LUA_NOREF;
    debug.mask = 0;
    debug.elapsed = 0;
    if (lua_isnoneornil(state, 1))
    {
        return 0;
    }

    luaL_checktype(state, 1, LUA_TFUNCTION);
    const auto* mask = luaL_checkstring(state, 2);
    for (const auto* p = mask; *p; ++p)
    {
        if (*p == 'l')
        {
            debug.mask |= LUA_MASKLINE;
        }

        if (*p == 'c')
        {
            debug.mask |= LUA_MASKCALL;
        }

        if (*p == 'r')
        {
            debug.mask |= LUA_MASKRET;
        }
    }

    debug.count = static_cast<int>(luaL_optinteger(state, 3, 0));
    if (debug.count > 0)
    {
        debug.mask |= LUA_MASKCOUNT;
    }

    lua_pushvalue(state, 1);
    debug.hook = luaL_ref(state, LUA_REGISTRYINDEX);
    return 0;
}

/// @brief Resolve only the private builtins and the owned loopback socket facade.
int lua_vm::require_debug_module()
{
    auto& debug = *debugger;
    const std::string_view name{luaL_checkstring(state, 1)};
    if (name == "socket.core")
    {
        lua_newtable(state);
        lua_pushinteger(state, 8);
        lua_pushcclosure(state, debug_native, 1);
        lua_setfield(state, -2, "tcp");
        return 1;
    }

    if (name != "math" && name != "string" && name != "table")
    {
        return luaL_error(state, "debugger module denied");
    }

    lua_rawgeti(state, LUA_REGISTRYINDEX, debug.environment);
    lua_getfield(state, -1, std::string{name}.c_str());
    return 1;
}

/// @brief Minimal LuaSocket-compatible surface and a single owned hook subscription.
int lua_vm::debug_operation(int code)
{
    auto& debug = *debugger;
    if (code == 0)
    {
        return set_debug_hook();
    }

    if (code == 1)
    {
        if (std::string_view{luaL_checkstring(state, 2)} != "127.0.0.1" ||
            luaL_checkinteger(state, 3) != debug.options.port)
        {
            return luaL_error(state, "loopback endpoint required");
        }

        debug.connection_failed = !debug.options.transport->connect(debug.options.port);
        if (!debug.connection_failed)
        {
            lua_pushinteger(state, 1);
            return 1;
        }

        lua_pushnil(state);
        lua_pushliteral(state, "closed");
        return 2;
    }

    if (code == 2)
    {
        std::size_t size = 0;
        const auto* bytes = luaL_checklstring(state, 2, &size);
        if (size > 1048576)
        {
            return luaL_error(state, "debugger message limit exceeded");
        }

        debug.options.transport->send({bytes, size});
        lua_pushinteger(state, static_cast<lua_Integer>(size));
        return 1;
    }

    if (code == 3)
    {
        const auto message = debug.options.transport->receive(debug.timeout);
        if (message.state == debug_message::status::ready)
        {
            if (message.line.size() > 1048576)
            {
                return luaL_error(state, "debugger message limit exceeded");
            }

            lua_pushlstring(state, message.line.data(), message.line.size());
            return 1;
        }

        lua_pushnil(state);
        lua_pushstring(state,
                       message.state == debug_message::status::closed ? "closed" : "timeout");
        return 2;
    }

    if (code == 4)
    {
        const auto timeout = luaL_checknumber(state, 2);
        if (!std::isfinite(timeout))
        {
            return luaL_error(state, "debugger timeout must be finite");
        }

        debug.timeout = std::clamp(timeout, 0.0, 0.25);
        return 0;
    }

    if (code == 5)
    {
        debug.options.transport->close();
        return 0;
    }

    if (code == 6)
    {
        return require_debug_module();
    }

    if (code == 8)
    {
        lua_newtable(state);
        int operation = 1;
        for (const auto* name : {"connect", "send", "receive", "settimeout", "close"})
        {
            lua_pushinteger(state, operation++);
            lua_pushcclosure(state, debug_native, 1);
            lua_setfield(state, -2, name);
        }

        return 1;
    }

    lua_pushnil(state);
    return 1;
}
} // namespace atlas::scripting::detail
