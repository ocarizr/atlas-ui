#pragma once
#include <atlas/diagnostics/logging.hpp>
#include <atlas/diagnostics/metrics.hpp>
#include <atlas/diagnostics/profiling.hpp>

namespace atlas::diagnostics
{
struct diagnostics_settings
{
    logging_options logging;
    profiling_options profiling;
    metrics_options metrics;
};
diagnostics_settings parse_settings(std::string_view yaml);
} // namespace atlas::diagnostics
