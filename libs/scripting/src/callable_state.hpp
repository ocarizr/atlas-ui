/** @file @brief Bounded registry metadata, private shared-VM contexts and transactional demands. */
#pragma once
#include "session_state.hpp"
#include <atlas/scripting/callables.hpp>
#include <map>

namespace atlas::scripting::detail
{
/// @brief Metadata collection runs only during protected module initialization.
struct callable_registration
{
    std::size_t bytes = 0;  ///< Copied metadata budget, independent of shared Lua strings.
    callable_origin origin; ///< Namespace whose IDs may be declared.
    std::vector<callable_descriptor> definitions; ///< No native or Lua pointers escape discovery.
};

/// @brief Pin the entry source and its complete immutable catalog lookup across live calls.
struct callable_module_state
{
    callable_module spec;          ///< Entry source and contributor namespace.
    std::string text, source_name; ///< Entry source copied at publication, not per frame.
    std::shared_ptr<const execution_bindings> sources; ///< Original immutable module capabilities.
    std::vector<callable_descriptor> definitions; ///< Expected discovery schema for lazy contexts.
};

/// @brief Never reorder an entry slot; replacing a module advances each affected generation.
struct callable_entry
{
    callable_descriptor descriptor;                  ///< Stable ID and typed protocol.
    callable_origin origin;                          ///< Contribution ownership.
    native_action action{};                          ///< Set only for native producers.
    native_condition condition{};                    ///< Set only for native predicates.
    std::shared_ptr<callable_module_state> module{}; ///< Present for Lua implementations.
    std::uint64_t generation = 1; ///< Reject stale bindings after replace/removal.
    bool active = true;           ///< Tombstones never execute.
};

/// @brief A runtime context shares the domain's physical interpreter, with private module locals.
struct callable_context
{
    std::string name;                                ///< Bound model-context name.
    std::shared_ptr<mvvm::view_model> model;         ///< Scoped source of truth.
    std::shared_ptr<callable_module_state> module{}; ///< Lua module generation.
    std::size_t native_entry = 0; ///< One-based native slot, zero for Lua module contexts.
    std::unique_ptr<lua_vm> vm{}; ///< Absent for native-only registries.
    std::shared_ptr<mailbox> mail = std::make_shared<mailbox>(); ///< Weak worker delivery.
    std::map<std::uint64_t, callable_binding> requests; ///< Owning bindings for admitted work.
    std::uint64_t next_request = 0; ///< Never reuse work IDs within this context.
    bool failed = false; ///< Lua errors require module reload before reusing private state.
};

/// @brief One invocation stages everything before model/native-work admission.
struct call_scope
{
    mvvm::view_model& model;             ///< Borrowed owner-affine state.
    session_limits limits;               ///< Existing scalar/command/text caps.
    bool read_only = false;              ///< Conditions cannot create demands.
    std::vector<mvvm::command> commands; ///< Private model transaction.
    std::vector<pending_work> requests;  ///< Owned value-only work descriptions.
    void demand();                       ///< Enforce permission and per-invocation command count.
};

/// @brief Domain-owned registry limits bound retained metadata, contexts and predicate work.
struct callable_state
{
    std::vector<callable_entry> entries; ///< At most 512 distinct IDs, including tombstones.
    std::map<std::uint64_t, std::unique_ptr<callable_context>>
        contexts;               ///< At most 128 live contexts.
    std::uint64_t sequence = 0; ///< Never reuse context identities.
    std::uint32_t calls = 0;    ///< Share the domain's configured per-frame call budget.
};

/// @brief Count copied descriptor payloads for the independent 8 MiB metadata budget.
std::size_t callable_metadata_bytes(const callable_descriptor& descriptor);

/// @brief Validate namespaced IDs and scalar/default/choice protocols before publication.
void validate_callable(const callable_origin& origin, const callable_descriptor& descriptor);
/// @brief Validate a descriptor independent of origin for code generation and Lua parsing.
void validate_callable_descriptor(const callable_descriptor& descriptor);
/// @brief Fill only omitted trailing defaults and reject all type/choice coercion.
std::vector<mvvm::value> callable_arguments(const callable_descriptor& descriptor,
                                            std::span<const mvvm::value> arguments,
                                            std::size_t max_text);
/// @brief Share one physical interpreter, while registration gets isolated libraries and globals.
std::unique_ptr<lua_vm> load_callable_vm(std::shared_ptr<mvvm::view_model> model,
                                         const callable_module_state& module, session_limits limits,
                                         const std::shared_ptr<lua_host>& host,
                                         std::shared_ptr<std::pmr::memory_resource> memory);
} // namespace atlas::scripting::detail
