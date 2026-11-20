/** @file @brief Protected registry DSL and injected scalar-only invocation capabilities. */
#include "lua_callable_fields.hpp"

namespace atlas::scripting::detail
{
/// @brief Discovery exposes registration only; model/native capabilities are not available.
void lua_vm::initialize_callables()
{
    lua_pushnil(state);
    lua_setglobal(state, "model");
    lua_newtable(state);
    handlers_ = luaL_ref(state, LUA_REGISTRYINDEX);
    for (const auto& item : {std::pair{"actions", -1}, std::pair{"navigation_conditions", -2}})
    {
        lua_newtable(state);
        lua_pushinteger(state, item.second);
        lua_pushcclosure(state, callable_native, 1);
        lua_setfield(state, -2, "register");
        lua_setglobal(state, item.first);
    }
}

/// @brief Preserve host error types through the same protected C++ Lua boundary as behaviors.
int lua_vm::callable_native(lua_State* state)
{
    auto& self = **static_cast<lua_vm**>(lua_getextraspace(state));
    try
    {
        const auto operation = static_cast<int>(lua_tointeger(state, lua_upvalueindex(1)));
        return operation < 0 ? self.register_callable(operation == -2)
                             : self.invoke_callable_context(operation);
    }
    catch (const std::exception&)
    {
        if (!self.host_error)
        {
            self.host_error = std::current_exception();
        }
    }

    return luaL_error(state, "callable registry boundary failed");
}

/// @brief Registration stores implementation references privately and copies only bounded metadata.
int lua_vm::register_callable(bool condition)
{
    if (!loading || !bindings || !bindings->registry || lua_gettop(state) != 1 ||
        !lua_istable(state, 1))
    {
        throw std::logic_error(
            "Callable registration requires module initialization and one descriptor");
    }

    callable_fields(state, 1,
                    {"id", "label", "category", "parameters", condition ? "evaluate" : "execute"});
    auto& registry = *bindings->registry;
    callable_descriptor descriptor;
    descriptor.id = callable_string(*this, 1, "id");
    descriptor.label = callable_string(*this, 1, "label");
    descriptor.category = callable_string(*this, 1, "category");
    descriptor.kind = condition ? callable_kind::condition : callable_kind::action;
    descriptor.parameters = read_callable_parameters(*this, 1);
    validate_callable(registry.origin, descriptor);
    if (registry.definitions.size() >= 512 || std::ranges::any_of(registry.definitions,
                                                                  [&](const auto& item)
                                                                  {
                                                                      return item.id ==
                                                                             descriptor.id;
                                                                  }))
    {
        throw std::invalid_argument("Duplicate callable ID or exhausted module capacity");
    }

    const auto bytes = callable_metadata_bytes(descriptor);
    if (bytes > std::size_t{8} * 1024 * 1024 - registry.bytes)
    {
        throw std::length_error("Callable module metadata budget exhausted");
    }

    registry.bytes += bytes;
    lua_rawgeti(state, LUA_REGISTRYINDEX, handlers_);
    lua_pushlstring(state, descriptor.id.data(), descriptor.id.size());
    lua_getfield(state, 1, condition ? "evaluate" : "execute");
    if (!lua_isfunction(state, -1))
    {
        throw std::invalid_argument("Callable descriptor requires a function");
    }

    lua_rawset(state, -3);
    lua_pop(state, 1);
    registry.definitions.push_back(std::move(descriptor));
    return 0;
}

/// @brief The caller cannot supply or retain an authored context parameter.
void lua_vm::push_callable_context()
{
    if (!callable_scope)
    {
        throw std::logic_error("Callable invocation requires a scoped context");
    }

    lua_newtable(state);
    int code = 0;
    for (const auto* name : {"get", "set", "invoke", "emit", "request"})
    {
        lua_pushinteger(state, code++);
        lua_pushcclosure(state, callable_native, 1);
        lua_setfield(state, -2, name);
    }
}

/// @brief Every producer call checks read-only permission before staging a command or request.
int lua_vm::invoke_callable_context(int operation)
{
    if (!callable_scope || loading || lua_gettop(state) < 1 || lua_gettop(state) > 33 ||
        lua_type(state, 1) != LUA_TSTRING)
    {
        throw std::invalid_argument(
            "Callable context needs a field/operation name and bounded arguments");
    }

    const auto name = std::get<std::string>(read(1));
    if (operation == 0)
    {
        if (lua_gettop(state) != 1)
        {
            throw std::invalid_argument("Context get requires one field name");
        }

        const condition_context context{*callable_scope};
        push(context.get(name));
        return 1;
    }

    callable_scope->demand();
    std::vector<mvvm::value> arguments;
    arguments.reserve(static_cast<std::size_t>(lua_gettop(state) - 1));
    for (int i = 2; i <= lua_gettop(state); ++i)
    {
        arguments.push_back(read(i));
    }

    action_context context{*callable_scope};
    if (operation == 1)
    {
        if (arguments.size() != 1)
        {
            throw std::invalid_argument("Context set requires one scalar value");
        }

        context.set(name, std::move(arguments.front()));
    }
    else if (operation == 2)
    {
        context.invoke(name, arguments);
    }
    else if (operation == 3)
    {
        context.emit(name, arguments);
    }
    else if (operation == 4)
    {
        context.request(name, arguments);
    }
    else
    {
        throw std::invalid_argument("Unknown callable context operation");
    }

    return 0;
}
} // namespace atlas::scripting::detail
