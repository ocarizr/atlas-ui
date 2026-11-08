/** @file @brief Script subscription producers validate slots without touching active listeners. */
#include "session_state.hpp"
#include <limits>
#include <stdexcept>

namespace atlas::scripting::detail
{
/// @brief Stage one bounded scoped edit; no subscriptions change during Lua execution.
int lua_vm::stage_listener(bool remove)
{
    if (loading || commands.size() + requests.size() + listeners.size() >= limits.max_commands)
    {
        throw std::logic_error("listener edits require an admitted bounded handler");
    }

    if (remove)
    {
        if (lua_gettop(state) != 1 || !lua_isinteger(state, 1) || lua_tointeger(state, 1) <= 0)
        {
            throw std::invalid_argument("unsubscribe needs a positive scoped token");
        }

        listeners.push_back({static_cast<std::uint64_t>(lua_tointeger(state, 1)), {}, {}});
        return 0;
    }

    if (lua_gettop(state) != 2 || !lua_isinteger(state, 1) || lua_tointeger(state, 1) < 4 ||
        lua_type(state, 2) != LUA_TSTRING)
    {
        throw std::invalid_argument("subscribe needs a resolved slot and handler name");
    }

    const auto encoded = static_cast<std::uint64_t>(lua_tointeger(state, 1));
    const auto raw = encoded / 4 - 1;
    if (raw > std::numeric_limits<std::uint32_t>::max() || encoded % 4 == 3)
    {
        throw std::invalid_argument("invalid subscription namespace or slot");
    }

    const auto index = static_cast<std::uint32_t>(raw);
    const auto& schema = model->description();
    listener_change change;
    if (encoded % 4 == 0)
    {
        change.slot = mvvm::field_handle{schema.identity(), index};
        (void)schema.type(std::get<mvvm::field_handle>(change.slot));
    }
    else if (encoded % 4 == 1)
    {
        if (index >= schema.describe().actions.size())
        {
            throw std::invalid_argument("invalid action subscription");
        }

        change.slot = mvvm::action_handle{schema.identity(), index};
    }
    else
    {
        if (index >= schema.describe().events.size())
        {
            throw std::invalid_argument("invalid event subscription");
        }

        change.slot = mvvm::event_handle{schema.identity(), index};
    }

    change.handler = std::get<std::string>(read(2));
    mvvm::validate_text(change.handler, 128);
    lua_rawgeti(state, LUA_REGISTRYINDEX, handlers_);
    lua_pushvalue(state, 2);
    lua_rawget(state, -2);
    if (!lua_isfunction(state, -1))
    {
        throw std::invalid_argument("script observer handler is missing");
    }

    lua_pop(state, 2);
    if (listener_sequence >= static_cast<std::uint64_t>(std::numeric_limits<lua_Integer>::max()))
    {
        throw std::overflow_error("script listener identities exhausted");
    }

    change.token = ++listener_sequence;
    listeners.push_back(std::move(change));
    lua_pushinteger(state, static_cast<lua_Integer>(listener_sequence));
    return 1;
}
} // namespace atlas::scripting::detail
