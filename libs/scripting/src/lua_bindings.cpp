/** @file @brief Exact typed slots and staged host demands without exposing pointers to scripts. */
#include "session_state.hpp"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace atlas::scripting::detail
{
/// @brief Account direct scalar records and owned text in bounded host transport.
std::size_t payload_bytes(std::span<const mvvm::value> values)
{
    std::size_t bytes = values.size() * sizeof(mvvm::value);
    for (const auto& value : values)
    {
        if (const auto* text = std::get_if<std::string>(&value))
        {
            bytes += text->size();
        }
    }

    return bytes;
}

/// @brief Reject composite, nonfinite, invalid XML text and excessive protocol payloads.
void validate_values(std::span<const mvvm::value> values, std::size_t max_text)
{
    if (values.size() > 32)
    {
        throw std::invalid_argument("script protocol exceeds 32 arguments");
    }

    std::size_t bytes = 0;
    for (const auto& value : values)
    {
        if (const auto* text = std::get_if<std::string>(&value))
        {
            if (text->size() > max_text - bytes)
            {
                throw std::overflow_error("script text budget exhausted");
            }

            mvvm::validate_text(*text, max_text);
            bytes += text->size();
        }

        if (const auto* number = std::get_if<double>(&value); number && !std::isfinite(*number))
        {
            throw std::invalid_argument("nonfinite script number");
        }
    }
}

/// @brief Preserve scalar types when copying caller values into the Lua stack.
void lua_vm::push(const mvvm::value& data)
{
    if (const auto* logical = std::get_if<bool>(&data))
    {
        lua_pushboolean(state, *logical);
    }
    else if (const auto* integer = std::get_if<std::int64_t>(&data))
    {
        lua_pushinteger(state, *integer);
    }
    else if (const auto* number = std::get_if<double>(&data))
    {
        lua_pushnumber(state, *number);
    }
    else
    {
        const auto& text = std::get<std::string>(data);
        lua_pushlstring(state, text.data(), text.size());
    }
}

/// @brief Copy bounded scalar results and reject implicit string or table coercion.
mvvm::value lua_vm::read(int index)
{
    switch (lua_type(state, index))
    {
        case LUA_TBOOLEAN:
            return lua_toboolean(state, index) != 0;
        case LUA_TNUMBER:
            if (lua_isinteger(state, index))
            {
                return static_cast<std::int64_t>(lua_tointeger(state, index));
            }
            return static_cast<double>(lua_tonumber(state, index));
        case LUA_TSTRING:
        {
            std::size_t size = 0;
            const auto* text = lua_tolstring(state, index, &size);
            if (size > limits.max_text_bytes)
            {
                throw std::overflow_error("script text value too large");
            }

            return std::string{text, size};
        }
        default:
            throw std::invalid_argument("script value must be a boolean, integer, number or text");
    }
}

/// @brief Convert native exceptions into protected errors while retaining their original type.
int lua_vm::native(lua_State* state)
{
    auto& self = **static_cast<lua_vm**>(lua_getextraspace(state));
    try
    {
        return self.invoke_native(static_cast<int>(lua_tointeger(state, lua_upvalueindex(1))));
    }
    catch (const std::exception&)
    {
        if (!self.host_error)
        {
            self.host_error = std::current_exception();
        }
    }

    return luaL_error(state, "model binding failed");
}

/// @brief Names resolve only when requested; returned integers preserve slot namespaces.
int lua_vm::resolve_name(int code)
{
    const auto& schema = model->description();
    if (lua_gettop(state) != 1 || lua_type(state, 1) != LUA_TSTRING)
    {
        throw std::invalid_argument("slot resolution needs one string name");
    }

    const auto name = std::get<std::string>(read(1));
    const auto slot = code == 0   ? schema.field(name).index
                      : code == 1 ? schema.action(name).index
                                  : schema.event(name).index;
    lua_pushinteger(state, (static_cast<lua_Integer>(slot) + 1) * 4 + code);
    return 1;
}

/// @brief Native operation descriptions are staged until model admission succeeds.
int lua_vm::request_work(std::vector<mvvm::value> arguments)
{
    if (lua_type(state, 1) != LUA_TSTRING || requests.size() == limits.max_pending_work)
    {
        throw std::invalid_argument("native request needs a bounded operation name");
    }

    const auto name = std::get<std::string>(read(1));
    mvvm::validate_text(name, 128);
    if (name.size() > limits.max_text_bytes - staged_text)
    {
        throw std::overflow_error("native request name exceeds aggregate text budget");
    }

    staged_text += name.size();
    if (name.empty() || next_request + requests.size() >=
                            static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
    {
        throw std::overflow_error("native request identity/name exhausted");
    }

    const auto id = next_request + requests.size() + 1;
    requests.push_back({id, name, std::move(arguments), {}});
    lua_pushinteger(state, static_cast<lua_Integer>(id));
    return 1;
}

/// @brief Stage typed host operations without publishing to model state during Lua execution.
int lua_vm::invoke_native(int code)
{
    const auto& schema = model->description();
    if (code < 3)
    {
        return resolve_name(code);
    }

    const auto kind = code == 7 ? 1 : code == 8 ? 2 : 0;
    if (code != 9 && (!lua_isinteger(state, 1) || lua_tointeger(state, 1) < 4 ||
                      lua_tointeger(state, 1) / 4 > std::numeric_limits<std::uint32_t>::max() ||
                      lua_tointeger(state, 1) % 4 != kind))
    {
        throw std::invalid_argument("model slot must be a positive cached index");
    }

    const auto index = code == 9 ? 0U : static_cast<std::uint32_t>(lua_tointeger(state, 1) / 4 - 1);
    const mvvm::field_handle field{schema.identity(), index};
    if (code == 3)
    {
        if (lua_gettop(state) != 1)
        {
            throw std::invalid_argument("model read needs one slot");
        }

        push(model->read(field));
        return 1;
    }

    if (loading)
    {
        throw std::logic_error("behavior initialization cannot produce demands");
    }

    if (lua_gettop(state) > 33 || commands.size() + requests.size() >= limits.max_commands)
    {
        throw std::overflow_error("script command/argument budget exhausted");
    }

    std::vector<mvvm::value> arguments;
    for (int i = 2; i <= lua_gettop(state); ++i)
    {
        arguments.push_back(read(i));
    }

    validate_values(arguments, limits.max_text_bytes);
    const auto text_bytes = payload_bytes(arguments) - arguments.size() * sizeof(mvvm::value);
    if (text_bytes > limits.max_text_bytes - staged_text)
    {
        throw std::overflow_error("script aggregate text budget exhausted");
    }

    staged_text += text_bytes;
    if (code == 9)
    {
        return request_work(std::move(arguments));
    }

    mvvm::command command{field, mvvm::update_kind::assign, std::move(arguments)};
    if (code == 5 || code == 6)
    {
        command.update = code == 5 ? mvvm::update_kind::add_integer : mvvm::update_kind::add_number;
    }
    else if (code == 7)
    {
        command.target = mvvm::action_handle{schema.identity(), index};
    }
    else if (code == 8)
    {
        command.target = mvvm::event_handle{schema.identity(), index};
    }

    commands.push_back(std::move(command));
    return 0;
}
} // namespace atlas::scripting::detail
