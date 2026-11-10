/** @file @brief Private domain registry with ordered instances and immutable source revisions. */
#pragma once
#include "session_state.hpp"
#include <atlas/scripting/domain.hpp>
#include <map>

namespace atlas::scripting
{
/// @brief One coordinator owns catalog publication, registrations and explicit phase boundaries.
class domain::impl
{
  public:
    session_limits limits; ///< Per-call/instance bounds and aggregate Lua memory cap.
    std::shared_ptr<std::pmr::memory_resource> memory; ///< Retained composition-root allocator.
    diagnostics::services destinations;            ///< Optional logging and profiling destinations.
    script_diagnostics_options script_diagnostics; ///< Shared capability policy for all instances.
    std::shared_ptr<detail::lua_host> interpreter; ///< Lazily created once per domain.
    std::map<std::string, std::shared_ptr<mvvm::view_model>, std::less<>>
        contexts; ///< Named model access points.
    using source_catalog = std::map<std::string, std::shared_ptr<const source_asset>,
                                    std::less<>>; ///< Shared immutable source metadata.
    using service_catalog = std::map<std::string, service_definition,
                                     std::less<>>; ///< Shared immutable capability signatures.
    std::shared_ptr<const source_catalog> sources =
        std::make_shared<source_catalog>(); ///< Copy only when publishing revisions.
    std::shared_ptr<const service_catalog> services =
        std::make_shared<service_catalog>(); ///< Reuse across instance generations.

    struct entry
    {
        instance_handle identity;          ///< Domain/registration/generation identity.
        behavior_spec spec;                ///< Authoring/catalog relationship.
        std::shared_ptr<session> behavior; ///< Logical execution context in the shared VM.
        bool enabled = true;               ///< Coordinator-controlled activation.
    }; ///< One ordered registration.

    std::vector<entry> entries;           ///< Stable registration order; no unordered iteration.
    std::vector<domain_message> messages; ///< Bounded owned ingress; never Lua registry references.
    std::size_t message_head = 0,
                message_bytes = 0;      ///< Consumed prefix and retained payload accounting.
    std::uint64_t message_sequence = 0; ///< Never accept a replayed or reordered ingress sequence.
    std::uint64_t identity = 0, sequence = 0, frame = 0; ///< Nonreused identity and frame policies.
    bool busy = false, closed = false;                   ///< Domain-wide reentry/lifetime guard.
    const std::thread::id owner = std::this_thread::get_id(); ///< Immutable coordinator affinity.
    void check() const;                 ///< Reject reentry, shutdown and foreign callers.
    entry& get(instance_handle handle); ///< Validate every identity component before VM access.
    std::shared_ptr<const detail::execution_bindings>
    snapshot() const; ///< Pin sources/services for one reload generation.
};
} // namespace atlas::scripting
