/** @file @brief Host composition of graph persistence with existing editor storage services. */
#pragma once
#include <atlas/editor_flow/persistence.hpp>
#include <atlas/jobs/job_system.hpp>

namespace atlas::editor
{
/// @brief Borrow the application worker pool for streamer reads and use atomic editor writes.
[[nodiscard]] editor_flow::storage make_flow_storage(jobs::job_system& jobs,
                                                     diagnostics::services sinks = {});
} // namespace atlas::editor
