#pragma once

#include <cstddef>
#include <memory_resource>

namespace atlas::jobs
{
struct scratch_options
{
    std::size_t block_size{4096};
    std::size_t max_reserved_bytes{std::size_t{4} * 1024 * 1024};
    std::pmr::memory_resource* upstream{std::pmr::new_delete_resource()};
};

struct scratch_statistics
{
    std::size_t allocation_calls{0};
    std::size_t requested_bytes{0};
    std::size_t reserved_bytes{0};
    std::size_t block_count{0};
};
} // namespace atlas::jobs
