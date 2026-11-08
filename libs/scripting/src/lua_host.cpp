/** @file @brief Physical Lua stack ownership and per-instance environment initialization. */
#include "director_source.hpp"
#include "session_state.hpp"
#include <stdexcept>

namespace atlas::scripting::detail
{
/// @brief Allocate one bounded interpreter without exposing any script capabilities.
lua_host::lua_host(session_limits policy, std::shared_ptr<std::pmr::memory_resource> resource)
    : memory{std::move(resource), 0, 0, policy.max_memory_bytes}
{
    state = lua_newstate(lua_vm::allocate, &memory, 0);
    if (!state)
    {
        throw std::bad_alloc{};
    }
}

/// @brief Logical contexts must be quiescent and released before closing their shared stack.
lua_host::~lua_host()
{
    if (executing || dispatching || owner != std::this_thread::get_id())
    {
        std::terminate();
    }

    lua_close(state);
}

/// @brief Retire only this context; the shared interpreter and other contexts stay alive.
void lua_vm::release_environment() noexcept
{
    for (const auto reference : {handlers_, environment_, loaded_, modules_})
    {
        luaL_unref(state, LUA_REGISTRYINDEX, reference);
    }
}

/// @brief Initialize an isolated global/loaded-table pair under the protected trampoline.
int lua_vm::initialize_environment()
{
    if (host->globals == LUA_NOREF)
    {
        lua_rawgeti(state, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
        host->globals = luaL_ref(state, LUA_REGISTRYINDEX);
    }

    if (host->loaded == LUA_NOREF)
    {
        lua_newtable(state);
        host->loaded = luaL_ref(state, LUA_REGISTRYINDEX);
    }

    if (host->director == LUA_NOREF)
    {
        if (luaL_loadbufferx(state, director_source, sizeof(director_source) - 1,
                             "@atlas/director.lua", "t") != LUA_OK)
        {
            return lua_error(state);
        }

        lua_call(state, 0, 1);
        host->director = luaL_ref(state, LUA_REGISTRYINDEX);
    }

    lua_newtable(state);
    lua_pushvalue(state, -1);
    environment_ = luaL_ref(state, LUA_REGISTRYINDEX);
    lua_rawseti(state, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
    lua_newtable(state);
    lua_pushvalue(state, -1);
    loaded_ = luaL_ref(state, LUA_REGISTRYINDEX);
    lua_setfield(state, LUA_REGISTRYINDEX, LUA_LOADED_TABLE);
    open_restricted_libraries();
    lua_newtable(state);
    int code = 0;
    for (const auto* name : {"field", "action", "event", "get", "set", "add_integer", "add_number",
                             "invoke", "emit", "request", "subscribe", "unsubscribe"})
    {
        lua_pushinteger(state, code++);
        lua_pushcclosure(state, native, 1);
        lua_setfield(state, -2, name);
    }

    lua_setglobal(state, "model");
    if (bindings && bindings->source)
    {
        lua_newtable(state);
        modules_ = luaL_ref(state, LUA_REGISTRYINDEX);
        lua_pushcfunction(state, module_native);
        lua_setglobal(state, "require");
    }

    if (bindings && bindings->service)
    {
        lua_newtable(state);
        lua_pushcfunction(state, service_native);
        lua_setfield(state, -2, "call");
        lua_setglobal(state, "native");
    }

    return 0;
}
} // namespace atlas::scripting::detail
