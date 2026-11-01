/**
 * @file
 * @brief Seeded map/fold/ordered-merge workloads for generic scheduler validation.
 */

#pragma once

#include <atlas/jobs/job_system.hpp>

#include <atomic>
#include <limits>
#include <memory>
#include <vector>

namespace atlas::jobs::workloads
{
/// @brief Seeded generic workload sizes and incremental dirty-selection policy.
struct workload_config
{
    /// @brief Number of synthetic input elements in the reproducible workload.
    std::size_t size{4096};
    /// @brief Caller-selected number of elements per dispatched range chunk.
    std::size_t grain{128};
    /// @brief Per-item transform iterations controlling useful work per scheduled chunk.
    std::size_t rounds{64};
    /// @brief Explicit deterministic input-generation seed.
    std::uint64_t seed{42};
    /// @brief Optional item selected to inject a deterministic workload failure.
    std::size_t fail_index{std::numeric_limits<std::size_t>::max()};
    std::size_t dirty_stride{1}; ///< 0 keeps the cached snapshot; >1 changes a subset.
};

/// @brief Owned immutable inputs and mutable phase outputs retained through the reference batch.
struct workload_state;

/// @brief One independently transformed synthetic input with its cached output state.
struct work_item
{
    /// @brief Deterministic original scalar transformed by the map stage.
    std::uint64_t input;
    /// @brief Cached mapped scalar reused when the item is clean.
    std::uint64_t output{0};
    /// @brief Borrowed parent workload state, retained by the surrounding submitted scope.
    workload_state* owner;
    /// @brief Graph/range-local index validated against its enclosing storage.
    std::size_t index;
    /// @brief Whether this item requires recomputation for the current incremental batch.
    bool dirty{true};
};

/// @brief One ordered range hash and its element count for deterministic merging.
struct partial_result
{
    /// @brief Ordered partial checksum used for deterministic reduction.
    std::uint64_t hash{0};
    /// @brief Number of elements represented by this ordered partial hash.
    std::size_t length{0};
};

/// @brief Owned immutable inputs and mutable phase outputs retained through the reference batch.
struct workload_state
{
    /// @brief Validated seeded workload configuration retained throughout execution.
    workload_config config;
    /// @brief Owned synthetic input items or their configured count.
    std::vector<work_item> items;
    /// @brief Owned partial hashes published by per-chunk folds before the ordered merge.
    std::vector<partial_result> partials;
    /// @brief Number of input elements visited by completed map chunks.
    std::atomic<std::size_t> mapped{0};
    /// @brief Number of elements included in completed partial folds.
    std::atomic<std::size_t> folded{0};
    /// @brief Number of dirty elements whose transform was recomputed in this batch.
    std::atomic<std::size_t> computed{0};
    /// @brief Current synthetic batch stage used to reject phase reentry or out-of-order execution.
    std::atomic<int> phase{0};
    /// @brief Deterministic ordered reference hash used to verify equivalent work.
    std::uint64_t checksum{0};
};

/// @brief Reference workload state plus the handle joining its submitted phases.
struct workload_run
{
    /// @brief Handle delimiting the map phase before dependent folds.
    task_handle map;
    /// @brief Handle delimiting partial folding before the ordered merge.
    task_handle fold;
    /// @brief Handle joining deterministic final reduction and output publication.
    task_handle merge;
};

/// @brief Build a reproducible seeded input snapshot and incremental cache state.
std::shared_ptr<workload_state> prepare_workload(workload_config config);
/// @brief Compute the sequential oracle for the same snapshot, rounds and ordering.
std::uint64_t sequential_reference(const workload_state& state);
/// @brief Compose map/fold/ordered-merge phases through the generic direct dispatch API.
workload_run submit_workload(const job_dispatcher& jobs,
                             const std::shared_ptr<workload_state>& state);
} // namespace atlas::jobs::workloads
