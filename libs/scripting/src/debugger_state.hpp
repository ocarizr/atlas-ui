/** @file @brief Development-only hook and environment ownership. */
#pragma once
#include "session_state.hpp"

namespace atlas::scripting::detail
{
/// @brief Private debugger capabilities never replace the application's restricted globals.
struct lua_vm::debug_state
{
    debugger_options options; ///< Verified source and retained local transport.
    int globals = LUA_NOREF, loaded = LUA_NOREF,
        environment = LUA_NOREF;                            ///< Saved registry references.
    int hook = LUA_NOREF, mask = 0, count = 0, elapsed = 0; ///< Requested Lua hook subscription.
    double timeout = 0;             ///< Socket-compatible poll/wait selection.
    bool connection_failed = false; ///< Detect a missing listener even if the agent catches errors.
};

} // namespace atlas::scripting::detail
