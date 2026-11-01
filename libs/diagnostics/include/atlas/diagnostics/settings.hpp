/**
 * @file
 * @brief Strict already-loaded YAML options without constructing collectors or opening files.
 */

#pragma once
#include <atlas/diagnostics/logging.hpp>
#include <atlas/diagnostics/metrics.hpp>
#include <atlas/diagnostics/profiling.hpp>

namespace atlas::diagnostics
{
/// @brief Parsed logging/profiling/metrics options; parsing does not construct collectors or open
/// storage.
struct diagnostics_settings
{
    /// @brief Parsed structured logging options; parsing alone creates no backend.
    logging_options logging;
    /// @brief Parsed bounded profiling options; parsing alone starts no capture.
    profiling_options profiling;
    /// @brief Parsed bounded metrics options; parsing alone records no observations.
    metrics_options metrics;
};

/// @brief Parse already-loaded YAML, rejecting unknown/duplicate keys and invalid options; never
/// open files.
///
/// @param yaml Already-loaded YAML bytes; this function never opens a file.
diagnostics_settings parse_settings(std::string_view yaml);
} // namespace atlas::diagnostics
