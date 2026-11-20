/** @file @brief Exact parameter/default/choice decoding from loaded Lua registration tables. */
#include "lua_callable_fields.hpp"

namespace atlas::scripting::detail
{
namespace
{
/// @brief Choices remain integer values and labels, never executable conversions.
std::vector<callable_choice> read_choices(lua_vm& vm, int parameter)
{
    lua_getfield(vm.state, parameter, "choices");
    std::vector<callable_choice> result;
    if (!lua_isnil(vm.state, -1))
    {
        const auto count = callable_array<64>(vm.state, -1);
        result.reserve(count);
        for (std::size_t i = 1; i <= count; ++i)
        {
            lua_rawgeti(vm.state, -1, static_cast<lua_Integer>(i));
            if (!lua_istable(vm.state, -1))
            {
                throw std::invalid_argument("Callable choice requires a record");
            }

            const auto record = lua_absindex(vm.state, -1);
            callable_fields(vm.state, record, {"label", "value"});
            const auto label = callable_string(vm, record, "label");
            lua_getfield(vm.state, record, "value");
            if (!lua_isinteger(vm.state, -1))
            {
                throw std::invalid_argument("Callable choice value must be integer");
            }

            result.push_back({label, static_cast<std::int64_t>(lua_tointeger(vm.state, -1))});
            lua_pop(vm.state, 2);
        }
    }

    lua_pop(vm.state, 1);
    return result;
}
} // namespace

/// @brief The registry validates ordering, defaults and names after structural decoding.
std::vector<callable_parameter> read_callable_parameters(lua_vm& vm, int descriptor)
{
    lua_getfield(vm.state, descriptor, "parameters");
    std::vector<callable_parameter> result;
    if (!lua_isnil(vm.state, -1))
    {
        const auto count = callable_array<32>(vm.state, -1);
        result.reserve(count);
        for (std::size_t i = 1; i <= count; ++i)
        {
            lua_rawgeti(vm.state, -1, static_cast<lua_Integer>(i));
            if (!lua_istable(vm.state, -1))
            {
                throw std::invalid_argument("Callable parameter requires a record");
            }

            const auto record = lua_absindex(vm.state, -1);
            callable_fields(vm.state, record, {"name", "type", "default", "choices"});
            callable_parameter parameter;
            parameter.name = callable_string(vm, record, "name");
            parameter.type = callable_type(callable_string(vm, record, "type"));
            lua_getfield(vm.state, record, "default");
            if (!lua_isnil(vm.state, -1))
            {
                parameter.default_value = vm.read(-1);
            }

            lua_pop(vm.state, 1);
            parameter.choices = read_choices(vm, record);
            result.push_back(std::move(parameter));
            lua_pop(vm.state, 1);
        }
    }

    lua_pop(vm.state, 1);
    return result;
}
} // namespace atlas::scripting::detail
