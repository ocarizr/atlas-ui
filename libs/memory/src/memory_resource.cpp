/** @file @brief Replaceable PMR resource implementation. */
#include <algorithm>
#include <atlas/memory/memory_resource.hpp>
#include <bit>
#include <limits>
#include <stdexcept>
#if defined(ATLAS_MEMORY_MIMALLOC)
#include <mimalloc.h>
#endif

namespace atlas::memory
{
/// @brief Store observation policy without changing process-global allocator state.
memory_resource::memory_resource(resource_options options) : options_{options}
{
}

/// @brief Expose the configured backend rather than implying all profiles use mimalloc.
std::string_view memory_resource::backend_name() const noexcept
{
#if defined(ATLAS_MEMORY_MIMALLOC)
    return "mimalloc";
#else
    return "system";
#endif
}

/// @brief Counters are observational snapshots, not one transaction across concurrent calls.
allocation_statistics memory_resource::statistics() const noexcept
{
    return {allocations_.load(std::memory_order_relaxed),
            deallocations_.load(std::memory_order_relaxed), live_.load(std::memory_order_relaxed),
            peak_.load(std::memory_order_relaxed)};
}

/// @brief Normalize empty requests while preserving caller-visible byte accounting.
void* memory_resource::do_allocate(std::size_t bytes, std::size_t alignment)
{
    if (!std::has_single_bit(alignment))
    {
        throw std::invalid_argument{"Allocation alignment must be a power of two"};
    }

    if (bytes > static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max()))
    {
        throw std::bad_alloc{};
    }

#if defined(ATLAS_MEMORY_MIMALLOC)
    void* pointer = mi_malloc_aligned(std::max(bytes, std::size_t{1}), alignment);
    if (!pointer)
    {
        throw std::bad_alloc{};
    }
#else
    void* pointer =
        std::pmr::new_delete_resource()->allocate(std::max(bytes, std::size_t{1}), alignment);
#endif
    if (options_.collect_statistics)
    {
        const auto current = live_.fetch_add(bytes, std::memory_order_relaxed) + bytes;
        auto maximum = peak_.load(std::memory_order_relaxed);
        while (maximum < current &&
               !peak_.compare_exchange_weak(maximum, current, std::memory_order_relaxed))
        {
        }

        allocations_.fetch_add(1, std::memory_order_relaxed);
    }

    return pointer;
}

/// @brief The PMR contract supplies the original size and alignment for paired deallocation.
void memory_resource::do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment)
{
#if defined(ATLAS_MEMORY_MIMALLOC)
    mi_free_size_aligned(pointer, std::max(bytes, std::size_t{1}), alignment);
#else
    std::pmr::new_delete_resource()->deallocate(pointer, std::max(bytes, std::size_t{1}),
                                                alignment);
#endif
    if (options_.collect_statistics)
    {
        live_.fetch_sub(bytes, std::memory_order_relaxed);
        deallocations_.fetch_add(1, std::memory_order_relaxed);
    }
}

/// @brief Equal backend names do not authorize bypassing another adapter's accounting.
bool memory_resource::do_is_equal(const std::pmr::memory_resource& other) const noexcept
{
    return this == &other;
}

/// @brief Share ownership without exposing vendor types or creating a per-thread private heap.
std::shared_ptr<memory_resource> make_resource(resource_options options)
{
    return std::make_shared<memory_resource>(options);
}
} // namespace atlas::memory
