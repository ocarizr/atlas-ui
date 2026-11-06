/** @file @brief Protected Lua C++ execution, restricted libraries and PMR allocation bounds. */
#include "session_state.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>

static_assert(LUA_VERSION_RELEASE_NUM == 50501, "Review the pinned Lua ABI before upgrading");
static_assert(std::is_same_v<lua_Number, double> && sizeof(lua_Integer) == sizeof(std::int64_t),
              "Script scalar conversion requires double and signed 64-bit Lua integers");

namespace atlas::scripting::detail
{
/// @brief Extra space carries a borrowed VM pointer; only owner-thread protected calls use it.
static lua_vm& context(lua_State* state)
{
    return **static_cast<lua_vm**>(lua_getextraspace(state));
}

/// @brief Implement bounded PMR reallocation without throwing through the allocator callback.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) -- Lua fixes this allocator ABI order.
void* lua_vm::allocate(void* context, void* pointer, std::size_t old_size,
                       std::size_t new_size) noexcept
{
    auto& memory = *static_cast<lua_memory*>(context);
    const auto previous = pointer ? old_size : 0;
    if (new_size > memory.limit - (memory.used - previous))
    {
        return nullptr;
    }

    void* replacement = nullptr;
    try
    {
        if (new_size)
        {
            replacement = memory.resource->allocate(new_size, alignof(std::max_align_t));
            if (pointer)
            {
                std::memcpy(replacement, pointer, std::min(old_size, new_size));
            }
        }

        if (pointer)
        {
            memory.resource->deallocate(pointer, old_size, alignof(std::max_align_t));
        }
    }
    catch (...)
    {
        return nullptr;
    }

    memory.used = memory.used - previous + new_size;
    memory.peak = std::max(memory.peak, memory.used);
    return replacement;
}

/// @brief Create a seeded interpreter and initialize permitted libraries under protection.
lua_vm::lua_vm(std::shared_ptr<mvvm::view_model> source, session_limits policy,
               std::shared_ptr<std::pmr::memory_resource> resource)
    : model(std::move(source)), limits(policy),
      memory{std::move(resource), 0, 0, policy.max_memory_bytes}
{
    commands.reserve(limits.max_commands);
    requests.reserve(limits.max_pending_work);
    state = lua_newstate(allocate, &memory, 0);
    if (!state)
    {
        throw std::bad_alloc{};
    }

    *static_cast<lua_vm**>(lua_getextraspace(state)) = this;
    try
    {
        protect();
    }
    catch (...)
    {
        lua_close(state);
        throw;
    }
}

/// @brief Release every Lua allocation before releasing the retained PMR resource.
lua_vm::~lua_vm()
{
    lua_close(state);
}

/// @brief Raise a protected quota error at fixed instruction-count intervals.
void lua_vm::hook(lua_State* state, lua_Debug*)
{
    auto& self = context(state);
    if (self.remaining <= 100)
    {
        luaL_error(state, "script instruction budget exhausted");
    }

    self.remaining -= 100;
}

/// @brief Restore stack/hook state and preserve original native failures across Lua dispatch.
void lua_vm::protect()
{
    const auto top = lua_gettop(state);

    /// @brief Restore stack balance even if constructing a native error message fails.
    struct stack_guard
    {
        lua_State* state; ///< Borrowed protected owner stack.
        int top;          ///< Restore even if copying an error message throws.

        ~stack_guard()
        {
            lua_settop(state, top);
        } ///< No script closers are exposed.
    } restore{state, top};

    remaining = limits.max_instructions;
    host_error = {};
    lua_sethook(state, hook, LUA_MASKCOUNT, 100);
    lua_pushcfunction(state, enter);
    const auto status = lua_pcall(state, 0, 0, 0);
    lua_sethook(state, nullptr, 0, 0);
    if (status != LUA_OK)
    {
        const char* message =
            lua_type(state, -1) == LUA_TSTRING ? lua_tostring(state, -1) : "non-text Lua failure";
        const std::string error{message};
        if (host_error)
        {
            std::rethrow_exception(host_error);
        }

        throw std::runtime_error(error);
    }
}

/// @brief Keep standard native exceptions distinct from Lua internal unwind exceptions.
int lua_vm::enter(lua_State* state)
{
    auto& self = context(state);
    try
    {
        return self.execute();
    }
    catch (const std::exception&)
    {
        self.host_error = std::current_exception();
    }

    return luaL_error(state, "native behavior boundary failed");
}

/// @brief Run the selected protected VM operation without exposing its Lua state.
int lua_vm::execute()
{
    auto& self = *this;
    if (self.operation_ == operation::initialize)
    {
        luaL_openselectedlibs(
            state, LUA_GLIBK | LUA_TABLIBK | LUA_STRLIBK | LUA_MATHLIBK | LUA_UTF8LIBK, 0);

        for (const auto* name :
             {"dofile", "loadfile", "load", "print", "warn", "collectgarbage", "pcall", "xpcall",
              "setmetatable", "getmetatable", "pairs", "next"})
        {
            lua_pushnil(state);
            lua_setglobal(state, name);
        }

        lua_getglobal(state, "math");
        for (const auto* name : {"random", "randomseed"})
        {
            lua_pushnil(state);
            lua_setfield(state, -2, name);
        }

        lua_pop(state, 1);
        lua_newtable(state);
        int code = 0;
        for (const auto* name : {"field", "action", "event", "get", "set", "add_integer",
                                 "add_number", "invoke", "emit", "request"})
        {
            lua_pushinteger(state, code++);
            lua_pushcclosure(state, native, 1);
            lua_setfield(state, -2, name);
        }

        lua_setglobal(state, "model");
        return 0;
    }

    if (self.operation_ == operation::load)
    {
        if (luaL_loadbufferx(state, self.text_.data(), self.text_.size(), "behavior", "t") !=
            LUA_OK)
        {
            return lua_error(state);
        }

        lua_call(state, 0, 1);
        if (!lua_istable(state, -1))
        {
            return luaL_error(state, "behavior must return a handler table");
        }

        self.handlers_ = luaL_ref(state, LUA_REGISTRYINDEX);
        return 0;
    }

    lua_rawgeti(state, LUA_REGISTRYINDEX, self.handlers_);
    lua_pushlstring(state, self.name_.data(), self.name_.size());
    lua_rawget(state, -2);
    self.found_ = lua_isfunction(state, -1);
    if (self.operation_ == operation::has || lua_isnil(state, -1))
    {
        return 0;
    }

    if (!self.found_)
    {
        return luaL_error(state, "behavior handler must be a function");
    }

    if (!lua_checkstack(state, static_cast<int>(self.arguments_.size()) + 2))
    {
        return luaL_error(state, "behavior argument stack exhausted");
    }

    for (const auto& argument : self.arguments_)
    {
        self.push(argument);
    }

    lua_call(state, static_cast<int>(self.arguments_.size()), LUA_MULTRET);
    if (lua_gettop(state) > 33)
    {
        return luaL_error(state, "behavior result limit exhausted");
    }

    for (int i = 2; i <= lua_gettop(state); ++i)
    {
        self.results_.push_back(self.read(i));
    }

    validate_values(self.results_, self.limits.max_text_bytes);
    return 0;
}

/// @brief Accept text-only source that returns the session handler table.
void lua_vm::load(std::string_view text)
{
    text_ = text;
    operation_ = operation::load;
    loading = true;
    protect();
    loading = false;
}

/// @brief Check a raw handler-table member without running script metamethods.
bool lua_vm::has(std::string_view name)
{
    operation_ = operation::has;
    name_ = name;
    protect();
    return found_;
}

/// @brief Clear prior staging before calling an optional handler and copying scalar results.
std::vector<mvvm::value> lua_vm::call(std::string_view name, std::span<const mvvm::value> args)
{
    operation_ = operation::call;
    name_ = name;
    arguments_ = args;
    results_.clear();
    commands.clear();
    requests.clear();
    staged_text = 0;
    protect();
    return std::move(results_);
}
} // namespace atlas::scripting::detail
