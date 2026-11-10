/** @file @brief Opt-in script diagnostic state kept outside the shared Lua stack. */
#pragma once
#include <atlas/scripting/session.hpp>

namespace atlas::scripting::detail
{
/// @brief Stable owner identity and reload generation for authored observations.
struct script_identity
{
    std::uint64_t domain = 0;     ///< Zero for standalone sessions.
    std::uint64_t instance = 0;   ///< Domain registration or standalone session identity.
    std::uint64_t generation = 0; ///< Successful load sequence.
};

/// @brief Allocate a nonreused process-local identifier for domains and standalone sessions.
std::uint64_t next_owner_identity();

/// @brief Destinations are borrowed from the owning session until its VM is retired.
struct authored_diagnostics
{
    diagnostics::logger* log;       ///< Optional structured log admission.
    diagnostics::profiler* profile; ///< Optional bounded CPU capture.
    diagnostics::metrics* metrics;  ///< Optional numeric registry.
    script_identity identity;       ///< Exact behavior generation.
    std::uint64_t frame = 0;        ///< Explicitly admitted logical frame.
    std::uint64_t request = 0;      ///< Native result ticket when handling on_result.
    std::size_t observations = 0;   ///< Shared bound across authored destinations per handler.
};
} // namespace atlas::scripting::detail
