/**
 * @file
 * @brief Replacement C++ allocation-request census used only by the workload benchmark.
 */

#pragma once
#include <cstdint>

namespace atlas::jobs::workloads
{
/// @brief Successful replacement C++ allocation requests/bytes, not live memory or OS allocation
/// counts.
struct allocation_counts
{
    /// @brief Successful replacement C++ allocation requests in the census window.
    std::uint64_t calls;
    /// @brief Requested bytes counted independently of reserved block/storage overhead.
    std::uint64_t requested_bytes;
};

/// @brief Reset and enable the benchmark's successful C++ allocation-request census.
void begin_allocation_counting() noexcept;
/// @brief Disable allocation counting and return requests/bytes observed in the census window.
allocation_counts end_allocation_counting() noexcept;
/// @brief Check replacement allocation entry points before using the benchmark's census results.
void verify_allocation_counter();
} // namespace atlas::jobs::workloads
