/** @file @brief Scalar-only bridge from LuaUnit to native test fixtures. */
#include "fixture.hpp"
#include <lauxlib.h>
#include <lua.h>
#include <stdexcept>
#include <type_traits>

namespace atlas::scripting::tests
{
namespace
{
/// @brief Share one case scope across runner stacks instead of copying it into a coroutine.
fixture*& active_fixture(lua_State* state)
{
    lua_rawgeti(state, LUA_REGISTRYINDEX, LUA_RIDX_MAINTHREAD);
    auto* main = lua_tothread(state, -1);
    lua_pop(state, 1);
    return *static_cast<fixture**>(lua_getextraspace(main));
}

/// @brief Reject fixture operations outside the callback that owns their native state.
fixture& current(lua_State* state)
{
    auto* active = active_fixture(state);
    if (!active)
    {
        throw std::logic_error("test fixture is outside with_fixture");
    }

    return *active;
}

/// @brief Borrow exact string bytes, preserving embedded NUL for production validation.
std::string_view string_arg(lua_State* state, int index)
{
    luaL_checktype(state, index, LUA_TSTRING);
    std::size_t size = 0;
    const auto* text = lua_tolstring(state, index, &size);
    return {text, size};
}

/// @brief Frame, revision, sequence and test handle IDs are positive integers.
std::uint64_t identity(lua_State* state, int index)
{
    const auto id = luaL_checkinteger(state, index);
    if (id <= 0)
    {
        throw std::invalid_argument("test fixture identity must be positive");
    }

    return static_cast<std::uint64_t>(id);
}

/// @brief Resolve fixture domain selection explicitly before entering the production API.
fixture_domain& side(lua_State* state)
{
    auto& test = current(state);
    const auto name = string_arg(state, 1);
    if (name == "ui")
    {
        return test.ui;
    }

    if (name == "application")
    {
        return test.application;
    }

    throw std::invalid_argument("test fixture domain must be ui or application");
}

/// @brief Preserve all original generation components on lookup.
fixture_instance instance(lua_State* state)
{
    return current(state).instances.at(static_cast<std::size_t>(identity(state, 1) - 1));
}

/// @brief Use the public scalar protocol, without sharing a Lua object across interpreters.
std::vector<mvvm::value> values(lua_State* state, int first)
{
    const auto last = lua_gettop(state);
    std::vector<mvvm::value> result;
    for (int i = first; i <= last; ++i)
    {
        switch (lua_type(state, i))
        {
            case LUA_TBOOLEAN:
                result.emplace_back(lua_toboolean(state, i) != 0);
                break;
            case LUA_TNUMBER:
                if (lua_isinteger(state, i))
                {
                    result.emplace_back(static_cast<std::int64_t>(lua_tointeger(state, i)));
                }
                else
                {
                    result.emplace_back(lua_tonumber(state, i));
                }
                break;
            case LUA_TSTRING:
                result.emplace_back(std::string{string_arg(state, i)});
                break;
            default:
                throw std::invalid_argument("test fixture protocol requires scalar values");
        }
    }

    return result;
}

/// @brief Return copied scalars to LuaUnit for assertions in the trusted harness.
void push_value(lua_State* state, const mvvm::value& data)
{
    std::visit(
        [state](const auto& item)
        {
            using type = std::remove_cvref_t<decltype(item)>; ///< Exact scalar alternative.
            if constexpr (std::is_same_v<type, bool>)
            {
                lua_pushboolean(state, item);
            }
            else if constexpr (std::is_same_v<type, std::int64_t>)
            {
                lua_pushinteger(state, item);
            }
            else if constexpr (std::is_same_v<type, double>)
            {
                lua_pushnumber(state, item);
            }
            else
            {
                lua_pushlstring(state, item.data(), item.size());
            }
        },
        data);
}

/// @brief Convert standard native errors while leaving Lua's internal C++ unwind intact.
template <int (*Operation)(lua_State*)> int protect(lua_State* state)
{
    try
    {
        return Operation(state);
    }
    catch (const std::exception& error)
    {
        lua_pushstring(state, error.what());
    }

    return lua_error(state);
}

/// @brief Release the entire native case before forwarding an assertion or Lua error to LuaUnit.
int with_fixture(lua_State* state)
{
    luaL_checktype(state, 1, LUA_TFUNCTION);
    auto& active = active_fixture(state);
    if (active)
    {
        throw std::logic_error("nested test fixtures are not supported");
    }

    int status = LUA_OK;
    {
        fixture test;

        /// @brief Clear the borrowed pointer before the fixture destructor runs.
        struct reset_scope
        {
            fixture*& pointer; ///< Runner-local borrow, valid only for this callback.

            ~reset_scope()
            {
                pointer = nullptr;
            } ///< Release the borrow even on a Lua C++ unwind.
        } scope{active};

        active = &test;
        lua_pushvalue(state, 1);
        status = lua_pcall(state, 0, 0, 0);
    }

    return status == LUA_OK ? 0 : lua_error(state);
}

/// @brief Publish an owned revision without allowing a behavior to open its source file.
int publish(lua_State* state)
{
    auto& target = side(state);
    const std::string name{string_arg(state, 2)};
    target.scripts.register_source({name, identity(state, 3), "@lua_behavior/" + name + ".lua",
                                    std::string{string_arg(state, 4)}});
    return 0;
}

/// @brief Return a test-local ID while retaining the native instance identity in the fixture.
int retain(lua_State* state, fixture_domain& owner, instance_handle handle)
{
    auto& items = current(state).instances;
    items.push_back({&owner, handle});
    lua_pushinteger(state, static_cast<lua_Integer>(items.size()));
    return 1;
}

/// @brief Create a behavior from the selected domain's catalog and fixture ViewModel.
int create(lua_State* state)
{
    auto& target = side(state);
    return retain(state, target,
                  target.scripts.create({std::string{string_arg(state, 2)}, "state"}));
}

/// @brief Retain the replacement without altering the previous generation's test ID.
int reload(lua_State* state)
{
    const auto target = instance(state);
    return retain(state, *target.owner, target.owner->scripts.reload(target.handle));
}

/// @brief Advance logical test time explicitly, admitting each model before its domain.
int begin_frame(lua_State* state)
{
    const auto frame = identity(state, 1);
    auto& test = current(state);
    for (auto* target : {&test.ui, &test.application})
    {
        target->model->begin_frame(frame);
        target->scripts.begin_frame(frame);
    }

    return 0;
}

/// @brief Route to the exact instance and copy the handler's scalar results.
int call(lua_State* state)
{
    const auto target = instance(state);
    const auto result =
        target.owner->scripts.call(target.handle, string_arg(state, 2), values(state, 3));
    luaL_checkstack(state, static_cast<int>(result.size()), "test result stack exhausted");
    for (const auto& data : result)
    {
        push_value(state, data);
    }

    return static_cast<int>(result.size());
}

/// @brief Dispatch a named handler in the domain's registration order.
int dispatch(lua_State* state)
{
    side(state).scripts.dispatch(string_arg(state, 2), values(state, 3));
    return 0;
}

/// @brief Inspect a committed model value outside behavior execution.
int read(lua_State* state)
{
    auto& model = *side(state).model;
    push_value(state, model.read(model.description().field(string_arg(state, 2))));
    return 1;
}

/// @brief Flush the model explicitly, independent of listener synchronization.
int flush(lua_State* state)
{
    side(state).model->flush();
    return 0;
}

/// @brief Expose a native domain phase as an explicit coordinator operation.
template <void (domain::*Operation)()> int domain_phase(lua_State* state)
{
    (side(state).scripts.*Operation)();
    return 0;
}

/// @brief Save one batch of requests so the test chooses the completion schedule.
int take_requests(lua_State* state)
{
    auto& requests = current(state).requests;
    requests = side(state).scripts.take_requests();
    lua_pushinteger(state, static_cast<lua_Integer>(requests.size()));
    return 1;
}

/// @brief Exercise the real worker completion transport synchronously and deterministically.
int complete(lua_State* state)
{
    const auto& request =
        current(state).requests.at(static_cast<std::size_t>(identity(state, 1) - 1));
    luaL_checktype(state, 2, LUA_TBOOLEAN);
    lua_pushboolean(state, request.work.complete(values(state, 3), lua_toboolean(state, 2) != 0));
    return 1;
}

/// @brief Submit owned scalar ingress under the production sequence/frame/generation checks.
int post(lua_State* state)
{
    const auto target = instance(state);
    target.owner->scripts.enqueue({target.handle, identity(state, 2), identity(state, 3),
                                   std::string{string_arg(state, 4)}, values(state, 5)});
    return 0;
}
} // namespace

/// @brief Install fixture operations only on the trusted LuaUnit stack.
void push_fixture_api(lua_State* state)
{
    active_fixture(state) = nullptr;
    const luaL_Reg methods[] = {
        {"with_fixture", protect<with_fixture>},
        {"publish", protect<publish>},
        {"create", protect<create>},
        {"reload", protect<reload>},
        {"begin_frame", protect<begin_frame>},
        {"call", protect<call>},
        {"dispatch", protect<dispatch>},
        {"read", protect<read>},
        {"flush", protect<flush>},
        {"synchronize", protect<domain_phase<&domain::synchronize>>},
        {"take_requests", protect<take_requests>},
        {"complete", protect<complete>},
        {"deliver_results", protect<domain_phase<&domain::deliver_results>>},
        {"post", protect<post>},
        {"deliver_messages", protect<domain_phase<&domain::deliver_messages>>},
        {nullptr, nullptr}};
    luaL_newlib(state, methods);
}
} // namespace atlas::scripting::tests
