/** @file @brief Strict bounded Lua descriptor fields under the protected registration trampoline.
 */
#pragma once
#include "callable_state.hpp"
#include <algorithm>

namespace atlas::scripting::detail
{
/// @brief Reject undeclared record fields rather than silently dropping typos.
inline void callable_fields(lua_State* state, int table,
                            std::initializer_list<std::string_view> names)
{
    table = lua_absindex(state, table);
    lua_pushnil(state);
    while (lua_next(state, table))
    {
        std::size_t length = 0;
        const auto* key =
            lua_type(state, -2) == LUA_TSTRING ? lua_tolstring(state, -2, &length) : nullptr;
        if (lua_type(state, -2) != LUA_TSTRING ||
            std::ranges::find(names, std::string_view{key, length}) == names.end())
        {
            throw std::invalid_argument("Unknown callable descriptor field");
        }

        lua_pop(state, 1);
    }
}

/// @brief Arrays must have exactly the consecutive integer keys reported by rawlen.
template <std::size_t Maximum> inline std::size_t callable_array(lua_State* state, int table)
{
    if (!lua_istable(state, table))
    {
        throw std::invalid_argument("Callable metadata requires an array");
    }

    table = lua_absindex(state, table);
    const auto size = lua_rawlen(state, table);
    if (size > Maximum)
    {
        throw std::length_error("Callable metadata array exceeds its budget");
    }

    lua_pushnil(state);
    while (lua_next(state, table))
    {
        if (!lua_isinteger(state, -2) || lua_tointeger(state, -2) < 1 ||
            static_cast<std::size_t>(lua_tointeger(state, -2)) > size)
        {
            throw std::invalid_argument("Callable metadata array has an invalid index");
        }

        lua_pop(state, 1);
    }

    return size;
}

/// @brief Name fields have no numeric-to-string conversion and no embedded NUL identity.
inline std::string callable_string(lua_vm& vm, int table, const char* field)
{
    lua_getfield(vm.state, table, field);
    if (lua_type(vm.state, -1) != LUA_TSTRING)
    {
        throw std::invalid_argument("Callable name field must be text");
    }

    auto result = std::get<std::string>(vm.read(-1));
    lua_pop(vm.state, 1);
    mvvm::validate_text(result, 128);
    return result;
}

/// @brief Keep authored float/string spelling independent of internal MVVM enum labels.
inline mvvm::value_type callable_type(std::string_view type)
{
    if (type == "boolean")
    {
        return mvvm::value_type::boolean;
    }

    if (type == "integer")
    {
        return mvvm::value_type::integer;
    }

    if (type == "float" || type == "number")
    {
        return mvvm::value_type::number;
    }

    if (type == "string" || type == "text")
    {
        return mvvm::value_type::text;
    }

    throw std::invalid_argument("Callable parameter requires boolean/string/integer/float type");
}

/// @brief Decode scalar/default/choice metadata without executing a registered implementation.
std::vector<callable_parameter> read_callable_parameters(lua_vm& vm, int descriptor);
} // namespace atlas::scripting::detail
