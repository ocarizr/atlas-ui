/** @file @brief Run trusted LuaUnit suites on the pinned Lua C++ ABI and report their status. */
#include "fixture.hpp"
#include <cstdio>
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#include <memory>

static_assert(LUA_VERSION_RELEASE_NUM == 50501,
              "Review LuaUnit and the Lua C++ ABI before upgrading");

namespace
{
/// @brief Explicit paths supplied by CTest; no package search is needed to load the framework.
struct arguments
{
    int count;    ///< Command-line size.
    char** paths; ///< Framework, runner, fixtures, report and suite paths.
};

/// @brief Load trusted test tooling as text and retain native Lua source diagnostics.
void load_script(lua_State* state, const char* path)
{
    if (luaL_loadfilex(state, path, "t") != LUA_OK)
    {
        lua_error(state);
    }
}

/// @brief Execute framework and suites on a dedicated stack, outside all production VMs.
int run_suites(lua_State* state)
{
    const auto& args = *static_cast<const arguments*>(lua_touserdata(state, 1));
    luaL_openlibs(state);
    load_script(state, args.paths[1]);
    lua_call(state, 0, 1);
    load_script(state, args.paths[2]);
    lua_insert(state, -2);
    atlas::scripting::tests::push_fixture_api(state);
    luaL_checkstack(state, args.count - 3, "Lua test arguments exceed stack capacity");
    for (int index = 3; index < args.count; ++index)
    {
        lua_pushstring(state, args.paths[index]);
    }

    lua_call(state, args.count - 1, 1);
    if (!lua_isinteger(state, -1))
    {
        return luaL_error(state, "Lua test runner must return a failure count");
    }

    return 1;
}
} // namespace

/// @brief Fail CTest when LuaUnit reports errors, no tests run, or tooling cannot load.
int main(int argc, char** argv)
{
    if (argc < 6)
    {
        std::fputs("Usage: atlas_lua_behavior_tests <luaunit.lua> <runner.lua> <fixture-directory> "
                   "<report.xml> <suite.lua> [suite.lua ...]\n",
                   stderr);
        return 1;
    }

    const std::unique_ptr<lua_State, decltype(&lua_close)> state{luaL_newstate(), lua_close};
    if (!state)
    {
        std::fputs("Cannot allocate LuaUnit interpreter\n", stderr);
        return 1;
    }

    arguments args{argc, argv};
    lua_pushcfunction(state.get(), run_suites);
    lua_pushlightuserdata(state.get(), &args);
    if (lua_pcall(state.get(), 1, 1, 0) != LUA_OK)
    {
        const auto* error = lua_tostring(state.get(), -1);
        std::fprintf(stderr, "Lua behavior tests: %s\n", error ? error : "non-text Lua error");
        return 1;
    }

    return lua_tointeger(state.get(), -1) == 0 ? 0 : 1;
}
