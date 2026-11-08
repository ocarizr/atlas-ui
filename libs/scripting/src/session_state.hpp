/** @file @brief Private serial VM ownership and thread-safe value-only completion transport. */
#pragma once
#include <atlas/scripting/session.hpp>
#include <functional>
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#include <mutex>
#include <thread>

namespace atlas::scripting::detail
{
/// @brief Apply the same bounded execution policy to standalone and domain interpreters.
void validate_limits(session_limits limits);

/// @brief Guard all public operations and synchronous diagnostic callbacks against VM reentry.
struct busy_guard
{
    bool& flag; ///< Borrowed owner-only guard.

    explicit busy_guard(bool& value) : flag(value)
    {
        flag = true;
    } ///< Mark guarded entry.

    ~busy_guard()
    {
        flag = false;
    } ///< Restore even after native/Lua exceptions.
};

/// @brief One retained request and result, ordered by submission rather than worker arrival.
struct pending_work
{
    std::uint64_t id = 0;                               ///< Generation-local sequence.
    std::string name;                                   ///< Native operation name.
    std::vector<mvvm::value> arguments, result;         ///< Independent owned transport values.
    bool issued = false, ready = false, success = true; ///< Single completion state.
};

/// @brief Transactional script listener edit, applied only at a coordinator safe boundary.
struct listener_change
{
    std::uint64_t token = 0; ///< Monotonic per-context identity.
    std::variant<mvvm::field_handle, mvvm::action_handle, mvvm::event_handle>
        slot;            ///< Validated namespace.
    std::string handler; ///< Empty only for an unsubscribe edit.
};

/// @brief Workers lock only bounded host transport and never enter a Lua stack.
class mailbox
{
  public:
    std::mutex mutex;                  ///< Protect every pending/closed access after publication.
    std::vector<pending_work> pending; ///< Reserved fixed maximum live entries.
    std::stop_source cancellation;     ///< Shared generation cancellation signal.
    std::size_t max_text = 0;          ///< Maximum text in one completion.
    std::size_t bytes = 0, limit = 0;  ///< Retained request/result payload bytes.
    bool closed = false;               ///< No further acceptance after session retirement.
    void close(); ///< Cancel without invoking Lua or joining caller-owned jobs.
};

/// @brief Validate bounded scalar protocols independently of Lua and native model publication.
void validate_values(std::span<const mvvm::value> values, std::size_t max_text);
std::size_t
payload_bytes(std::span<const mvvm::value> values); ///< Owned scalar/text transport size.

/// @brief Retained PMR used by Lua's nonthrowing realloc-compatible callback.
struct lua_memory
{
    std::shared_ptr<std::pmr::memory_resource> resource; ///< Outlives lua_close.
    std::size_t used = 0, peak = 0, limit = 0; ///< Requested live bytes, not resident memory.
};

/// @brief One physical interpreter shared by independent owner-thread execution contexts.
struct lua_host
{
    std::vector<mvvm::command> commands;    ///< Reused serial command scratch.
    std::vector<pending_work> requests;     ///< Reused serial request scratch.
    std::vector<listener_change> listeners; ///< Reused serial listener scratch.
    lua_memory memory;          ///< Domain-wide Lua allocation budget and retained resource.
    lua_State* state = nullptr; ///< Physical stack, never exposed publicly.
    int globals = LUA_NOREF, loaded = LUA_NOREF,
        director = LUA_NOREF;                    ///< Root registry references.
    bool executing = false, dispatching = false; ///< Prevent cross-instance interpreter/reentry.
    const std::thread::id owner = std::this_thread::get_id(); ///< Fixed interpreter owner.
    lua_host(session_limits policy,
             std::shared_ptr<std::pmr::memory_resource> resource); ///< Create without libraries.
    ~lua_host(); ///< Close only after all logical contexts release their references.
};

/// @brief Borrowed immutable catalog bytes live through the retained lookup snapshot.
struct module_source
{
    std::string_view text, name; ///< Source bytes and stable authoring identity.
};

/// @brief Optional project-provided capabilities; no storage or native SDK implementation here.
struct execution_bindings
{
    std::function<module_source(std::string_view)> source; ///< Snapshot-scoped catalog lookup.
    std::function<std::vector<mvvm::value>(std::string_view, std::span<const mvvm::value>)>
        service; ///< Validated synchronous project service.
};

/// @brief One protected interpreter; C++ Lua linkage provides RAII-safe exception unwinding.
class lua_vm
{
  public:
    /// @brief Create a private VM with PMR allocation and restricted libraries.
    lua_vm(std::shared_ptr<mvvm::view_model> source, session_limits policy,
           std::shared_ptr<std::pmr::memory_resource> resource,
           std::shared_ptr<lua_host> shared = {},
           std::shared_ptr<const execution_bindings> bindings = {});
    ~lua_vm();                                 ///< lua_close while model/resource are still alive.
    lua_vm(const lua_vm&) = delete;            ///< Lua states cannot be copied.
    lua_vm& operator=(const lua_vm&) = delete; ///< One serial owner.
    void load(std::string_view text, std::string_view source_name); ///< Named protected text load.
    void attach(debugger_options&& options); ///< Protected private debugger environment.
    void detach() noexcept;                  ///< Retire transport and Lua hook references.
    bool debugging() const noexcept;         ///< Whether development timing labels are required.
    int hook_mask() const noexcept; ///< Union of quota and optional debugger subscriptions.
    static int debug_native(lua_State* state); ///< Development transport/hook bridge.
    void debug_event(lua_Debug* event); ///< Multiplex LuaPanda notifications with owned quotas.
    /// @brief Protected invocation with per-call demand and result staging.
    std::vector<mvvm::value> call(std::string_view name, std::span<const mvvm::value> args);
    bool has(std::string_view name);     ///< Protected raw handler-table lookup.
    static int enter(lua_State* state);  ///< Protected initialization/load/call/check trampoline.
    static int native(lua_State* state); ///< Typed model commands and native work requests.
    static void hook(lua_State* state, lua_Debug* debug); ///< Instruction quota trap.
    static void* allocate(void* context, void* pointer, std::size_t old_size,
                          std::size_t new_size) noexcept; ///< PMR failures become Lua OOM.
    void push(const mvvm::value& data);                   ///< Exact scalar conversion to Lua.
    mvvm::value read(int index); ///< Reject nil/tables and preserve integer versus number type.
    std::shared_ptr<mvvm::view_model> model; ///< Retained independent source of truth.
    session_limits limits;                   ///< Immutable checked policies.
    std::shared_ptr<lua_host> host; ///< Retain the single domain interpreter or standalone host.
    std::shared_ptr<const execution_bindings>
        bindings;                         ///< Immutable capabilities captured at load.
    lua_memory& memory;                   ///< Shared physical interpreter accounting.
    lua_State* state = nullptr;           ///< Never escapes the library.
    std::vector<mvvm::command>& commands; ///< Shared transactional execution scratch.
    std::vector<pending_work>&
        requests; ///< Shared requests transferred before another instance executes.
    std::vector<listener_change>& listeners; ///< Shared edits transferred into the instance queue.
    std::uint64_t listener_sequence = 0; ///< Caller-supplied committed observer identity counter.
    std::exception_ptr host_error;  ///< Preserve a native exception through protected Lua dispatch.
    std::uint64_t next_request = 0; ///< Committed request identity before this call.
    std::uint32_t remaining = 0;    ///< Per-call instruction budget.
    std::size_t staged_text = 0;    ///< Aggregate command/request text in the current call.
    bool loading = false;           ///< Initialization cannot produce commands/work.
    bool privileged = false; ///< Only trusted debugger initialization bypasses script quotas.
    struct debug_state;      ///< Optional source, transport and Lua registry references.
    std::shared_ptr<debug_state> debugger; ///< Absent in normal sessions.
  private:
    enum class operation : std::uint8_t
    {
        initialize, ///< Open the permitted libraries and host bindings.
        load,       ///< Run text-only initialization and retain the returned handler table.
        call,       ///< Execute a selected handler.
        has,        ///< Check handler presence without invoking it.
        attach      ///< Initialize optional development tooling under protection.
    }; ///< Protected trampoline selector.
    operation operation_ = operation::initialize; ///< One guarded operation at a time.
    std::string_view text_, name_;                ///< Borrowed until protected invocation returns.
    std::string source_name_; ///< Owned null-terminated source identity for Lua debug information.
    std::span<const mvvm::value> arguments_; ///< Caller-owned stable input through call.
    std::vector<mvvm::value> results_;       ///< Copied before leaving the protected C++ frame.
    bool found_ = false;                     ///< Protected lookup result.
    int handlers_ = LUA_NOREF;               ///< Registry-owned returned table.
    int environment_ = LUA_NOREF, loaded_ = LUA_NOREF; ///< Per-instance globals and module state.
    int modules_ = LUA_NOREF;                    ///< Private per-instance catalog module cache.
    std::vector<std::string> imports_;           ///< Bounded dependency stack for cycle detection.
    static int module_native(lua_State* state);  ///< Protected catalog loader bridge.
    static int service_native(lua_State* state); ///< Protected project service bridge.
    int call_service();             ///< Validate bounded scalar input/output around a service.
    std::size_t service_calls_ = 0; ///< Per-handler native-service invocation budget.
    int load_module();              ///< Resolve only caller-supplied catalog bytes, never files.
    int initialize_environment();   ///< Create isolated standard libraries and model bindings.
    void release_environment() noexcept; ///< Retire this instance's registry roots.
    void
    open_restricted_libraries(); ///< Private standard libraries without forbidden capabilities.
    int invoke_native(int code); ///< May throw native or Lua exceptions inside a protected call.
    int resolve_name(int code);  ///< Resolve one authored name into a namespace-tagged slot.
    int request_work(std::vector<mvvm::value> arguments); ///< Stage a bounded native operation.
    int stage_listener(
        bool remove); ///< Validate and stage subscribe/unsubscribe without model mutation.
    int execute();    ///< Protected trampoline body with native exception preservation.
    void protect(); ///< Restore the Lua stack and rethrow original host errors or Lua diagnostics.
    int initialize_debugger();     ///< Private debugger globals and protocol startup.
    int debug_operation(int code); ///< Protected native debugger operation.
    int set_debug_hook();          ///< Replace only the debugger's owned hook subscription.
    int require_debug_module();    ///< Resolve the small private debugger module vocabulary.
    void restore_debug_globals();  ///< Restore original application globals after setup.
};
} // namespace atlas::scripting::detail

namespace atlas::scripting
{
/// @brief Session state is shared only through weak observers, never through workers.
class session::impl : public std::enable_shared_from_this<session::impl>
{
  public:
    std::shared_ptr<mvvm::view_model> model;           ///< Retained owner model.
    session_limits limits;                             ///< Validated generation policy.
    std::shared_ptr<std::pmr::memory_resource> memory; ///< Retained allocator.
    diagnostics::services destinations;                ///< Optional lifecycle and CPU diagnostics.
    std::unique_ptr<detail::lua_vm> vm;                ///< Current successful generation.
    std::shared_ptr<detail::lua_host> interpreter; ///< Optional domain-owned shared interpreter.
    std::shared_ptr<const detail::execution_bindings>
        bindings;                          ///< Catalog/service snapshot for the next load.
    std::shared_ptr<detail::mailbox> mail; ///< Fresh identity on every successful reload.

    /// @brief One session token paired with the model's nonreused subscription identity.
    struct watch_entry
    {
        std::uint64_t id;         ///< Lifetime session watch token.
        mvvm::subscription token; ///< Model subscription to release before VM teardown.
    };

    std::vector<watch_entry> watches;                      ///< At most 1024 retained registrations.
    std::vector<detail::listener_change> listener_changes; ///< Deferred edits awaiting synchronize.
    std::uint64_t sequence = 0; ///< Never reuse watch identity across reloads.
    statistics stats;           ///< Current owner observations.
    bool busy = false, closed = false,
         delivering = false; ///< Reentry and terminal lifecycle guards.
    const std::thread::id owner = std::this_thread::get_id(); ///< Immutable thread affinity.
    void check() const; ///< Reject foreign/reentrant/closed calls before mutable access.
    void detach();      ///< Remove model subscriptions without executing scripts.
    void fail();        ///< Latch failure and request native-work cancellation.
    void admit();       ///< Publish validated producer demands and deferred listener edits.
    /// @brief Guard dispatch and commit the successful producer batch.
    std::vector<mvvm::value> call(std::string_view name, std::span<const mvvm::value> args,
                                  bool internal = false,
                                  std::span<const mvvm::value_type> result_types = {});
    std::uint64_t
    watch(std::variant<mvvm::field_handle, mvvm::action_handle, mvvm::event_handle> slot,
          std::string_view handler,
          std::uint64_t reserved_token = 0); ///< Resolve and retain a weak VM observer.
    void synchronize_listeners();            ///< Commit a bounded batch outside every model flush.
};
} // namespace atlas::scripting
