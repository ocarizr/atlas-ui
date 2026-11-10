/** @file @brief Bounded log, duration and numeric observations from protected Lua handlers. */
#include "session_state.hpp"
#include <array>
#include <cmath>
#include <stdexcept>

namespace atlas::scripting::detail
{
namespace
{
/// @brief Bound all authored emissions before any optional destination admission.
void consume(authored_diagnostics& output)
{
    if (output.observations >= 64)
    {
        throw std::overflow_error("script diagnostic observation budget exhausted");
    }

    ++output.observations;
}

/// @brief Borrow an exact Lua string held on the protected stack.
std::string_view text_at(lua_State* state, int index)
{
    if (lua_type(state, index) != LUA_TSTRING)
    {
        throw std::invalid_argument("script diagnostic expects a string");
    }

    std::size_t size = 0;
    const auto* bytes = lua_tolstring(state, index, &size);
    return {bytes, size};
}

/// @brief Keep names and categories bounded with an explicit stable alphabet.
std::string_view key_at(lua_State* state, int index)
{
    const auto key = text_at(state, index);
    if (key.empty() || key.size() > 64 ||
        key.find_first_not_of(
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-") !=
            std::string_view::npos)
    {
        throw std::invalid_argument("invalid script diagnostic key");
    }

    return key;
}

/// @brief Format provenance only when an injected sink will receive it.
std::string origin(const authored_diagnostics& output, std::string_view source)
{
    const auto& identity = output.identity;
    auto result = "domain=" + std::to_string(identity.domain) +
                  " instance=" + std::to_string(identity.instance) +
                  " generation=" + std::to_string(identity.generation) +
                  " frame=" + std::to_string(output.frame);
    if (output.request)
    {
        result += " request=" + std::to_string(output.request);
    }

    result += " source=";
    result += source;
    return result;
}

/// @brief An authored severity is independent of the injected logger's filtering policy.
diagnostics::log_level severity(std::string_view name)
{
    constexpr std::array levels{std::string_view{"trace"}, std::string_view{"debug"},
                                std::string_view{"info"},  std::string_view{"warning"},
                                std::string_view{"error"}, std::string_view{"critical"}};
    for (std::size_t i = 0; i < levels.size(); ++i)
    {
        if (levels[i] == name)
        {
            return static_cast<diagnostics::log_level>(i);
        }
    }

    throw std::invalid_argument("invalid script diagnostic severity");
}

/// @brief Log admission is observational and does not mutate the producer command batch.
bool log(lua_State* state, authored_diagnostics& output, std::string_view source)
{
    if (lua_gettop(state) != 3)
    {
        throw std::invalid_argument("diagnostic.log expects severity, category and message");
    }

    const auto level = severity(text_at(state, 1));
    const auto category = key_at(state, 2);
    const auto message = text_at(state, 3);
    mvvm::validate_text(message, 2048);
    if (!output.log)
    {
        return false;
    }

    consume(output);
    auto attributed = "[" + origin(output, source) + "] ";
    attributed += message;
    return output.log->write(level, "lua." + std::string{category}, attributed);
}

/// @brief Copy at most eight raw dimensions; Lua metamethods never run during observation.
diagnostics::metric_attributes dimensions(lua_State* state)
{
    diagnostics::metric_attributes result;
    if (lua_isnoneornil(state, 3))
    {
        return result;
    }

    lua_pushnil(state);
    while (lua_next(state, 3))
    {
        if (result.size() == 8)
        {
            throw std::invalid_argument("script diagnostic dimension limit exceeded");
        }

        const auto key = key_at(state, -2);
        const auto value = text_at(state, -1);
        mvvm::validate_text(value, 128);
        result.emplace_back(key, value);
        lua_pop(state, 1);
    }

    return result;
}

/// @brief Observe a finite scalar through the registry's existing kind and cardinality rules.
bool metric(lua_State* state, authored_diagnostics& output, diagnostics::metric_kind kind)
{
    if (lua_gettop(state) < 2 || lua_gettop(state) > 3 || lua_type(state, 2) != LUA_TNUMBER ||
        (!lua_isnoneornil(state, 3) && lua_type(state, 3) != LUA_TTABLE))
    {
        throw std::invalid_argument(
            "diagnostic metric expects name, number and optional dimensions");
    }

    const auto key = key_at(state, 1);
    const auto value = lua_tonumber(state, 2);
    if (!std::isfinite(value) || (kind == diagnostics::metric_kind::counter && value < 0))
    {
        throw std::invalid_argument("invalid script diagnostic metric value");
    }

    if (!output.metrics)
    {
        return false;
    }

    consume(output);
    const auto name = "lua." + std::string{key};
    const auto attributes = dimensions(state);
    return kind == diagnostics::metric_kind::counter
               ? output.metrics->increment(name, value, attributes)
               : output.metrics->set(name, value, attributes);
}

/// @brief Own all borrowed labels while Lua executes or unwinds the callback.
struct duration
{
    diagnostics::profiler& capture; ///< Borrowed live profiler.
    std::string name;               ///< Owned authored label.
    std::string origin;             ///< Owned source and invocation provenance.
    diagnostics::profiler::clock::time_point start =
        diagnostics::profiler::clock::now(); ///< Start after constructing all observation labels.
    int exceptions = std::uncaught_exceptions(); ///< C++ Lua unwinding baseline.

    ~duration()
    {
        diagnostics::event_context context;
        context.result = std::uncaught_exceptions() > exceptions ? "failed" : "succeeded";
        context.origin = origin;
        (void)capture.duration(name, "lua.scope", start, diagnostics::profiler::clock::now(),
                               context);
    } ///< Record exactly once on normal return or protected Lua/native unwinding.
};

/// @brief Preserve normal Lua result and error behavior under every capture policy.
int call_region(lua_vm& vm, std::string_view source)
{
    auto* state = vm.state;
    const auto name = key_at(state, 1);
    if (!lua_isfunction(state, 2))
    {
        throw std::invalid_argument("diagnostic.scope expects a Lua function");
    }

    const auto arguments = lua_gettop(state) - 2;
    auto& output = *vm.authored;
    if constexpr (diagnostics::instrumentation_enabled)
    {
        if (output.profile && output.profile->enabled() && !vm.debugging())
        {
            consume(output);
            const duration observed{*output.profile, std::string{name}, origin(output, source)};
            lua_call(state, arguments, LUA_MULTRET);
            return lua_gettop(state) - 1;
        }
    }

    lua_call(state, arguments, LUA_MULTRET);
    return lua_gettop(state) - 1;
}
} // namespace

/// @brief Install a private table in only the explicitly opted-in behavior environment.
void lua_vm::install_diagnostics()
{
    lua_newtable(state);
    int code = 0;
    for (const auto* name : {"log", "scope", "increment", "gauge"})
    {
        lua_pushinteger(state, code++);
        lua_pushcclosure(state, diagnostic_native, 1);
        lua_setfield(state, -2, name);
    }

    lua_setglobal(state, "diagnostic");
}

/// @brief Preserve native failures and let Lua's own exception reach the protected trampoline.
int lua_vm::diagnostic_native(lua_State* state)
{
    auto& vm = **static_cast<lua_vm**>(lua_getextraspace(state));
    try
    {
        return vm.observe_diagnostic(static_cast<int>(lua_tointeger(state, lua_upvalueindex(1))));
    }
    catch (const std::exception&)
    {
        vm.host_error = std::current_exception();
    }

    return luaL_error(state, "script diagnostic failed");
}

/// @brief Only an admitted handler may produce authored observations, including on_result.
int lua_vm::observe_diagnostic(int code)
{
    if (!authored || operation_ != operation::call || loading || !authored->frame)
    {
        throw std::logic_error("script diagnostics require an admitted handler");
    }

    if (code == 1)
    {
        return call_region(*this, source_name_);
    }

    const auto accepted = code == 0 ? log(state, *authored, source_name_)
                                    : metric(state, *authored,
                                             code == 2 ? diagnostics::metric_kind::counter
                                                       : diagnostics::metric_kind::gauge);
    lua_pushboolean(state, accepted);
    return 1;
}
} // namespace atlas::scripting::detail
