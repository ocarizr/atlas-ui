/**
 * @file
 * @brief Typed reusable action/condition contracts owned by a scripting domain.
 */
#pragma once
#include <atlas/scripting/session.hpp>
#include <functional>

namespace atlas::scripting
{
namespace detail
{
struct call_scope; ///< Ephemeral owner-only invocation state.
class lua_vm;      ///< Protected bridge on the shared owner interpreter.
} // namespace detail
class domain; ///< Registry and interpreter owner.

/// @brief Scope metadata never grants authority to replace another origin's registrations.
enum class callable_origin_kind : std::uint8_t
{
    engine,  ///< Built-in tooling contribution.
    project, ///< Project-owned category.
    add_on   ///< Optional extension contribution.
};

/// @brief Stable namespace prefix, independent of category labels or physical module paths.
struct callable_origin
{
    std::string name; ///< IDs must begin with this prefix followed by a dot.
    callable_origin_kind kind = callable_origin_kind::project; ///< Discovery/inspector grouping.
    bool operator==(const callable_origin&) const = default;   ///< Exact owner identity.
};
/// @brief Registered actions produce demands; conditions return exactly one boolean.
enum class callable_kind : std::uint8_t
{
    action,   ///< Deferred demand producer.
    condition ///< Read-only boolean predicate.
};

/// @brief Optional integer presentation metadata without introducing a fifth scalar type.
struct callable_choice
{
    std::string label;                                       ///< Inspector caption.
    std::int64_t value;                                      ///< Exact stored integer.
    bool operator==(const callable_choice&) const = default; ///< Stable schema comparison.
};

/// @brief Defaults may fill omitted trailing arguments; conversion is never implicit.
struct callable_parameter
{
    std::string name; ///< Stable parameter name within a descriptor.
    mvvm::value_type type = mvvm::value_type::boolean; ///< Exact scalar protocol.
    std::optional<mvvm::value> default_value{};        ///< Omission requires an explicit default.
    std::vector<callable_choice> choices{};            ///< Integer-only optional inspector choices.
    bool operator==(const callable_parameter&) const = default; ///< Reload schema comparison.
};

/// @brief Inspector-visible schema shared by native callbacks and registered Lua functions.
struct callable_descriptor
{
    std::string id;       ///< Stable namespaced identity; not a display caption.
    std::string label;    ///< Human-readable inspector caption.
    std::string category; ///< Presentation grouping, independent of identity.
    std::vector<callable_parameter> parameters{}; ///< At most 32 scalar parameters.
    callable_kind kind = callable_kind::action;   ///< Supplied by registration API/table.
    bool operator==(const callable_descriptor&) const = default; ///< Strict reload equivalence.
};

/// @brief Resolve a catalog-backed entry module; modules never open files themselves.
struct callable_module
{
    std::string source;     ///< Registered source identity imported by the entrypoint.
    callable_origin origin; ///< Namespace and contribution scope.
};

/// @brief One domain, registration generation and bound model context; no Lua pointers escape.
struct callable_binding
{
    std::uint64_t domain = 0;                                 ///< Exact domain owner identity.
    std::uint64_t sequence = 0;                               ///< Stable registration slot.
    std::uint64_t generation = 0;                             ///< Registration lifetime.
    std::uint64_t context = 0;                                ///< Exact bound model context.
    bool operator==(const callable_binding&) const = default; ///< Reject every stale component.
};

/// @brief A read capability borrowed only during the current synchronous callback.
class condition_context
{
  public:
    mvvm::value get(std::string_view field) const; ///< Read committed model state by stable name.
    condition_context(const condition_context&) =
        delete; ///< Never retain borrowed invocation state.
    condition_context& operator=(const condition_context&) = delete; ///< Preserve scoped lifetime.
  protected:
    friend class domain;         ///< Only the owning registry creates a capability.
    friend class detail::lua_vm; ///< Protected Lua calls receive the same scoped capability.
    explicit condition_context(detail::call_scope& scope); ///< Borrow one invocation.
    detail::call_scope& scope_; ///< Never shared with a worker or retained by a descriptor.
};

/// @brief Actions stage complete demands and publish only after successful callback return.
class action_context : public condition_context
{
  public:
    void set(std::string_view field, mvvm::value value); ///< Queue an exact typed field assignment.
    void invoke(std::string_view action,
                std::span<const mvvm::value> arguments = {}); ///< Queue a model action.
    void emit(std::string_view event,
              std::span<const mvvm::value> arguments = {}); ///< Queue a model event.
    void request(std::string_view operation,
                 std::span<const mvvm::value> arguments = {}); ///< Queue owned native work.
  private:
    friend class domain;         ///< Only the owning registry supplies a write capability.
    friend class detail::lua_vm; ///< Protected Lua calls stage through the same transaction.
    explicit action_context(detail::call_scope& scope); ///< Borrow one transactional invocation.
};

using native_action = std::function<void(
    action_context&, std::span<const mvvm::value>)>; ///< Cooperative bounded producer.
using native_condition = std::function<bool(
    const condition_context&, std::span<const mvvm::value>)>; ///< Read-only predicate.

/// @brief A bound leaf or an AND group; negation applies only to successfully evaluated values.
struct condition_node
{
    callable_binding binding{}; ///< Nonzero sequence selects a leaf; zero selects a group.
    std::vector<mvvm::value> arguments{};   ///< Authored scalar inputs for a leaf.
    std::vector<condition_node> children{}; ///< Ordered children for an AND group.
    bool negate = false; ///< Never invert errors or unresolved bindings into permission.
};
/// @brief Invalid bindings and execution failures remain distinct from a legitimate false result.
enum class condition_status : std::uint8_t
{
    passed,  ///< Successfully granted permission.
    blocked, ///< Successfully denied permission.
    invalid, ///< Unresolved or malformed expression.
    failed   ///< Implementation or execution-budget failure.
};

/// @brief Owned result independent of the temporary Lua stack or native invocation.
struct condition_evaluation
{
    condition_status status =
        condition_status::invalid; ///< Fail closed until successfully evaluated.
    std::string diagnostic{};      ///< Owned failure description, empty for ordinary true/false.

    bool allowed() const noexcept
    {
        return status == condition_status::passed;
    } ///< Successful permission only.
};

/// @brief CPU workers receive owned values and an existing generation-safe completion mailbox.
struct callable_work
{
    callable_binding source; ///< Originating action and bound context.
    work_request work;       ///< Complete from a worker without entering Lua or UI state.
};

/// @brief Completion reports are accepted only on the owner in deterministic context/request order.
struct callable_result
{
    callable_binding source;         ///< Exact originating registration generation.
    std::uint64_t request = 0;       ///< Context-local work identity.
    bool success = false;            ///< Explicit worker completion outcome.
    std::vector<mvvm::value> values; ///< Owned scalar transport for later owner-phase handling.
};

/// @brief Generate a category module with typed descriptors and contributor annotations.
[[nodiscard]] std::string
generate_callable_category(std::span<const callable_descriptor> definitions);
/// @brief Generate a deterministic entrypoint importing registered catalog source identities.
[[nodiscard]] std::string generate_callable_entrypoint(std::span<const std::string> sources);
} // namespace atlas::scripting
