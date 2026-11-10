/**
 * @file
 * @brief Owner-thread Lua behavior with deferred model commands and bounded native work mailboxes.
 */
#pragma once
#include <atlas/mvvm/view_model.hpp>
#include <atlas/scripting/debugger.hpp>
#include <atlas/scripting/diagnostics.hpp>
#include <stop_token>

namespace atlas::scripting
{
namespace detail
{
class mailbox; ///< Thread-safe completion transport; never contains a Lua state.
}

/// @brief Bounded native work description; workers only see owned values and cancellation.
class work_request
{
  public:
    std::uint64_t id = 0; ///< Session generation-local identity, never a Lua reference.
    std::string name;     ///< Host-registered operation vocabulary.
    std::vector<mvvm::value> arguments; ///< Owned immutable-by-convention job inputs.
    std::stop_token cancellation;       ///< Requested on reload, failure or shutdown.
    /// @brief Submit exactly one bounded result without touching the interpreter.
    bool complete(std::span<const mvvm::value> values = {}, bool success = true) const;
    ///< Thread-safe single acceptance; false after duplicate, cancellation or stale generation.
  private:
    friend class session; ///< Only admitted session requests receive transport access.
    std::weak_ptr<detail::mailbox> destination_; ///< Does not extend VM/session lifetime.
    std::uint64_t ticket_ = 0; ///< Immutable transport identity, independent of public metadata.
};

/// @brief Per-session and per-call bounds; Lua C library work is not wall-clock preemptible.
struct session_limits
{
    std::size_t max_memory_bytes = std::size_t{4} * 1024 * 1024; ///< Lua live bytes, cap 64 MiB.
    std::size_t max_source_bytes = 262144;                       ///< Text-only source, cap 1 MiB.
    std::size_t max_commands = 64;                               ///< Per-call demands, cap 4096.
    std::size_t max_text_bytes = 65536; ///< Per-call command/input/result text, cap 1 MiB.
    std::size_t max_pending_work = 64;  ///< Session-wide native work entries, cap 1024.
    std::size_t max_pending_bytes = std::size_t{4} * 1024 * 1024; ///< Retained payload, cap 64 MiB.
    std::uint32_t max_calls = 256;           ///< Per-frame VM calls, cap 65536.
    std::uint32_t max_instructions = 100000; ///< Per protected call including nested script code.
};

/// @brief Owner observations; byte accounting excludes host command payloads and vendor heaps.
struct statistics
{
    std::uint64_t frame = 0;        ///< Current frame selected by the composition root.
    std::uint32_t calls = 0;        ///< Attempted calls in the current frame.
    std::size_t lua_bytes = 0;      ///< Current requested Lua allocation bytes.
    std::size_t lua_peak_bytes = 0; ///< Peak live Lua bytes for the current generation.
    std::size_t pending_work = 0;   ///< Includes issued/completed work awaiting owner acceptance.
    bool failed = false;            ///< Reload required; model committed state is retained.
};

/// @brief One logical Lua behavior/model; a domain can share its physical interpreter.
class session
{
  public:
    explicit session(
        std::shared_ptr<mvvm::view_model> model, session_limits limits = {},
        std::shared_ptr<std::pmr::memory_resource> memory = {},
        diagnostics::services destinations = {},
        script_diagnostics_options script_diagnostics = {}); ///< Retain infrastructure.
    ~session(); ///< Cancel transport and release observers/VM on the owner, outside callbacks.
    session(const session&) = delete;            ///< A Lua stack has one execution owner.
    session& operator=(const session&) = delete; ///< No mutable VM aliases.
    void load(std::string_view source,
              std::string_view source_name = "behavior"); ///< Named text load.
    static bool
    debugger_available() noexcept; ///< Compiled development capability; false in shipping.
    void attach_debugger(debugger_options options); ///< Attach after load, outside frame callbacks.
    void detach_debugger(); ///< Disconnect without exposing debug APIs to application scripts.
    void begin_frame(std::uint64_t frame); ///< Require the matching healthy model frame.
    /// @brief Run an optional named handler and admit its demands only after successful return.
    std::vector<mvvm::value> call(std::string_view handler,
                                  std::span<const mvvm::value> arguments = {},
                                  std::span<const mvvm::value_type> result_types = {});
    ///< Missing handlers are optional; successful calls atomically admit model demands.
    std::uint64_t watch(mvvm::field_handle field, std::string_view handler); ///< Compile observer.
    std::uint64_t watch(mvvm::action_handle action,
                        std::string_view handler);                           ///< Compile observer.
    std::uint64_t watch(mvvm::event_handle event, std::string_view handler); ///< Compile observer.
    void unwatch(std::uint64_t token); ///< Idempotent owner cleanup, including during callbacks.
    void synchronize(); ///< Apply script-created listener edits outside model flushes.
    std::vector<work_request> take_requests(); ///< Transfer unissued native work at owner boundary.
    void deliver_results();     ///< Deliver ready submission-order prefix to optional on_result.
    void shutdown();            ///< Idempotent cancellation; no script execution or worker join.
    statistics inspect() const; ///< Owner-only observations.
  private:
    friend class domain; ///< Domain composition shares the physical interpreter, not public Lua
                         ///< state.
    class impl; ///< Private Lua C++ linkage, protected trampoline and serial command staging.
    std::shared_ptr<impl> impl_; ///< Observers borrow weakly to avoid ownership cycles.
};
} // namespace atlas::scripting
