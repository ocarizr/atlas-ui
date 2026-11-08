/** @file @brief Restricted module loading from immutable catalog snapshots supplied by the caller.
 */
#include "session_state.hpp"
#include <algorithm>
#include <stdexcept>

namespace atlas::scripting::detail
{
/// @brief Keep original project/catalog failures across protected Lua error conversion.
int lua_vm::module_native(lua_State* state)
{
    auto& self = **static_cast<lua_vm**>(lua_getextraspace(state));
    try
    {
        return self.load_module();
    }
    catch (const std::exception&)
    {
        self.host_error = std::current_exception();
    }

    return luaL_error(state, "catalog module failed");
}

/// @brief Cache only within this instance and reject recursive or excessively deep imports.
int lua_vm::load_module()
{
    if (!bindings || !bindings->source || lua_gettop(state) != 1 ||
        lua_type(state, 1) != LUA_TSTRING)
    {
        throw std::invalid_argument("require needs one catalog identity");
    }

    const auto identity = std::get<std::string>(read(1));
    mvvm::validate_text(identity, 128);
    lua_rawgeti(state, LUA_REGISTRYINDEX, modules_);
    lua_pushvalue(state, 1);
    lua_rawget(state, -2);
    if (!lua_isnil(state, -1))
    {
        return 1;
    }

    lua_pop(state, 1);
    if (imports_.size() >= 32 ||
        std::find(imports_.begin(), imports_.end(), identity) != imports_.end())
    {
        throw std::invalid_argument("cyclic or excessive catalog module dependency");
    }

    imports_.push_back(identity);

    /// @brief C++ Lua unwinding always releases the current dependency-stack entry.
    struct import_guard
    {
        std::vector<std::string>& stack; ///< Borrowed owner-only import stack.

        ~import_guard()
        {
            stack.pop_back();
        } ///< Unwind after both native and Lua failures.
    } restore{imports_};

    const auto source = bindings->source(identity);
    const std::string source_name{source.name};
    if (luaL_loadbufferx(state, source.text.data(), source.text.size(), source_name.c_str(), "t") !=
        LUA_OK)
    {
        return lua_error(state);
    }

    lua_call(state, 0, 1);
    if (lua_isnil(state, -1))
    {
        throw std::invalid_argument("catalog module must return a value");
    }

    lua_pushvalue(state, 1);
    lua_pushvalue(state, -2);
    lua_rawset(state, -4);
    return 1;
}
} // namespace atlas::scripting::detail
