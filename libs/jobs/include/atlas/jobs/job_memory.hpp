/**
 * @file
 * @brief Per-task monotonic scratch budgets and terminal allocation statistics.
 */

#pragma once

#include <cstddef>
#include <memory_resource>

namespace atlas::jobs
{
/// @brief Per-task monotonic block/budget policy with a caller-owned concurrent upstream allocator.
struct scratch_options
{
    /// @brief Preferred scratch block size in bytes.
    std::size_t block_size{4096};
    /// @brief Maximum reserved scratch storage per used task, excluding allocator metadata.
    std::size_t max_reserved_bytes{std::size_t{4} * 1024 * 1024};
    /// @brief Borrowed upstream allocator; it must outlive allocations and support concurrent
    /// scopes.
    std::pmr::memory_resource* upstream{std::pmr::new_delete_resource()};
};

/// @brief Terminal scratch usage; storage has already been reclaimed when completed handles expose
/// it.
struct scratch_statistics
{
    /// @brief Successful scratch allocation requests observed in the scope.
    std::size_t allocation_calls{0};
    /// @brief Requested bytes counted independently of reserved block/storage overhead.
    std::size_t requested_bytes{0};
    /// @brief High-water reserved scratch block bytes before bulk reclamation.
    std::size_t reserved_bytes{0};
    /// @brief Number of scratch blocks obtained by the scope.
    std::size_t block_count{0};
};
} // namespace atlas::jobs
