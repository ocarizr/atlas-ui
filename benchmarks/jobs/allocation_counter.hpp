#pragma once
#include <cstdint>

namespace atlas::jobs::workloads
{
struct allocation_counts
{
    std::uint64_t calls;
    std::uint64_t requested_bytes;
};
void begin_allocation_counting() noexcept;
allocation_counts end_allocation_counting() noexcept;
void verify_allocation_counter();
} // namespace atlas::jobs::workloads
