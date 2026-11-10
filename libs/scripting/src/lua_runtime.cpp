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
               std::shared_ptr<std::pmr::memory_resource> resource,
               std::shared_ptr<lua_host> shared,
               std::shared_ptr<const execution_bindings> capabilities,
               std::unique_ptr<authored_diagnostics> diagnostics)
    : model(std::move(source)), limits(policy),
      host(shared ? std::move(shared) : std::make_shared<lua_host>(policy, std::move(resource))),
      bindings(std::move(capabilities)), memory(host->memory), commands(host->commands),
      requests(host->requests), listeners(host->listeners), authored(std::move(diagnostics))
{
    commands.reserve(limits.max_commands);
    requests.reserve(limits.max_pending_work);
    state = host->state;

    *static_cast<lua_vm**>(lua_getextraspace(state)) = this;
    try
    {
        protect();
    }
    catch (...)
    {
        release_environment();
        throw;
    }
}

/// @brief Release every Lua allocation before releasing the retained PMR resource.
lua_vm::~lua_vm()
{
    detach();
    release_environment();
}

/// @brief Raise a protected quota error at fixed instruction-count intervals.
void lua_vm::hook(lua_State* state, lua_Debug* event)
{
    auto& self = context(state);
    if (event->event == LUA_HOOKCOUNT && !self.privileged)
    {
        const auto interval = self.debugging() ? 1U : 100U;
        if (self.remaining <= interval)
        {
            luaL_error(state, "script instruction budget exhausted");
        }

        self.remaining -= interval;
    }

    self.debug_event(event);
}

/// @brief Restore stack/hook state and preserve original native failures across Lua dispatch.
void lua_vm::protect()
{
    if (host->executing || host->owner != std::this_thread::get_id())
    {
        throw std::logic_error("shared Lua interpreter is busy or foreign");
    }

    const busy_guard execution{host->executing};
    *static_cast<lua_vm**>(lua_getextraspace(state)) = this;
    if (environment_ != LUA_NOREF)
    {
        lua_rawgeti(state, LUA_REGISTRYINDEX, environment_);
        lua_rawseti(state, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
        lua_rawgeti(state, LUA_REGISTRYINDEX, loaded_);
        lua_setfield(state, LUA_REGISTRYINDEX, LUA_LOADED_TABLE);
    }

    const auto top = lua_gettop(state);

    /// @brief Restore stack balance even if constructing a native error message fails.
    struct stack_guard
    {
        lua_State* state; ///< Borrowed protected owner stack.
        int top;          ///< Restore even if copying an error message throws.
        lua_host& host;   ///< Shared root environment restored after every entry.

        ~stack_guard()
        {
            lua_sethook(state, nullptr, 0, 0);
            lua_settop(state, top);
            if (host.globals != LUA_NOREF)
            {
                lua_rawgeti(state, LUA_REGISTRYINDEX, host.globals);
                lua_rawseti(state, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
                lua_rawgeti(state, LUA_REGISTRYINDEX, host.loaded);
                lua_setfield(state, LUA_REGISTRYINDEX, LUA_LOADED_TABLE);
            }
        } ///< No script closers are exposed.
    } restore{state, top, *host};

    remaining = limits.max_instructions;
    host_error = {};
    lua_sethook(state, hook, hook_mask(), debugging() ? 1 : 100);
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
    if (self.operation_ == operation::attach)
    {
        return initialize_debugger();
    }

    if (self.operation_ == operation::initialize)
    {
        return initialize_environment();
    }

    if (self.operation_ == operation::load)
    {
        if (luaL_loadbufferx(state, self.text_.data(), self.text_.size(), self.source_name_.c_str(),
                             "t") != LUA_OK)
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

    if (!lua_checkstack(state, static_cast<int>(self.arguments_.size()) + 4))
    {
        return luaL_error(state, "behavior argument stack exhausted");
    }

    lua_pop(state, 1);
    lua_rawgeti(state, LUA_REGISTRYINDEX, host->director);
    lua_pushvalue(state, 1);
    lua_pushlstring(state, self.name_.data(), self.name_.size());
    for (const auto& argument : self.arguments_)
    {
        self.push(argument);
    }

    lua_call(state, static_cast<int>(self.arguments_.size()) + 2, LUA_MULTRET);
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
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) -- Mirror Lua's source/chunk-name order.
void lua_vm::load(std::string_view text, std::string_view source_name)
{
    text_ = text;
    source_name_ = source_name;
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
    listeners.clear();
    staged_text = 0;
    service_calls_ = 0;
    protect();
    return std::move(results_);
}
} // namespace atlas::scripting::detail
