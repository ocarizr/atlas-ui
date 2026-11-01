/**
 * @file
 * @brief Optional adaptation of terminal jobs into independent logging and profiling destinations.
 */

#pragma once
#include <atlas/diagnostics/logging.hpp>
#include <atlas/diagnostics/profiling.hpp>
#include <atlas/jobs/job_system.hpp>

namespace atlas::jobs_diagnostics
{
/// At least one destination is required. Destinations outlive all accepted observations.
std::shared_ptr<jobs::job_observer> make_observer(std::shared_ptr<diagnostics::logger> log,
                                                  std::shared_ptr<diagnostics::profiler> profile);
} // namespace atlas::jobs_diagnostics
