/**
 * @file
 * @brief Explicit host policy for diagnostics authored by Lua behaviors.
 */
#pragma once
#include <atlas/diagnostics/metrics.hpp>

namespace atlas::scripting
{
/// @brief Expose diagnostics only when a composition root opts in for its session or domain.
struct script_diagnostics_options
{
    bool enabled = false; ///< Install the diagnostic table in behavior environments.
    std::shared_ptr<diagnostics::metrics> metrics{}; ///< Optional bounded numeric registry.
};
} // namespace atlas::scripting
