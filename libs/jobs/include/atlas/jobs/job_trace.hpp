/**
 * @file
 * @brief Owned diagnostic labels and bounded incremental-task trace snapshots.
 */

#pragma once

#include <atlas/jobs/job_graph.hpp>
#include <atlas/jobs/job_memory.hpp>
#include <atlas/jobs/job_policy.hpp>

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace atlas::jobs
{
/// @brief Owned task name/category; categories describe observations and do not select priority.
struct job_label
{
    /// @brief Initialize owned task name/category; categories describe observations and do not
    /// select priority.
    ///
    /// @param value Owned task name.
    /// @param group Optional diagnostic category.
    job_label(std::string value, std::string group = {})
        : name{std::move(value)}, category{std::move(group)}
    {
    }

    /// @brief Initialize owned task name/category; categories describe observations and do not
    /// select priority.
    ///
    /// @param value Task name copied into owned storage; null produces an empty name.
    job_label(const char* value) : job_label{value ? std::string{value} : std::string{}}
    {
    }

    /// @brief Owned diagnostic task name, copied from temporary caller labels.
    std::string name;
    /// @brief Owned diagnostic category; an empty child category inherits from its parent.
    std::string category;
};

/// @brief Bounded direct-task record with runtime-local IDs and observational timing.
struct task_trace
{
    /// @brief Runtime-local task identity used for diagnostic correlation.
    std::uint64_t id{0};
    /// @brief Runtime-local parent identity; zero denotes an independent root.
    std::uint64_t parent_id{0};
    /// @brief Monotonic runtime-local dispatch order for observational scheduling diagnostics.
    std::uint64_t dispatch_index{0};
    /// @brief Effective weighted dispatch class; it does not imply callback preemption.
    job_priority priority{job_priority::normal};
    /// @brief Owned or call-scoped diagnostic name, according to the enclosing observation
    /// contract.
    std::string name;
    /// @brief Diagnostic category, or grouped borrowed record views; categories do not imply
    /// priority.
    std::string category;
    /// @brief IDs/handles that must complete before this work becomes eligible.
    std::vector<std::uint64_t> prerequisites;
    /// @brief Original worker slot used for execution attribution.
    std::size_t worker_index{0};
    /// @brief Whether the callback executed through its caller-owned notification queue.
    bool caller_thread{false};
    /// @brief Whether notification ownership pins delivery to its submitting thread/worker.
    bool affinity_bound{false};
    /// @brief Observed task state; suppressed bodies did not execute user work.
    job_status status{job_status::pending};
    /// @brief Nanoseconds from runtime creation to task admission.
    std::chrono::nanoseconds submitted{};
    /// @brief Nanoseconds from runtime creation to callback dispatch/start.
    std::chrono::nanoseconds started{};
    /// @brief Nanoseconds from runtime creation to body return, before descendant joins.
    std::chrono::nanoseconds body_finished{};
    /// @brief Nanoseconds from runtime creation to terminal scoped completion.
    std::chrono::nanoseconds completed{};
    /// @brief Terminal scratch statistics retained after task storage reclamation.
    scratch_statistics scratch;
};

/// @brief Owning bounded trace snapshot with explicit pending and dropped counts.
struct job_trace_report
{
    /// @brief Whether this explicitly configured capture/checker is active.
    bool enabled;
    /// @brief Configured persistent worker count; must be positive.
    std::size_t worker_count;
    /// @brief Direct tasks excluded because trace capacity was exhausted.
    std::size_t dropped;
    /// @brief Admitted trace records not yet terminal when this snapshot was copied.
    std::size_t pending;
    /// @brief Owned bounded direct-task trace records.
    std::vector<task_trace> tasks;
};
} // namespace atlas::jobs
