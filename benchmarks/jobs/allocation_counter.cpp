/**
 * @file
 * @brief Replacement C++ allocation-request census used only by the workload benchmark.
 */

#include "allocation_counter.hpp"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <new>

// Preserve the CRT's declared contracts when MSVC /analyze checks replacements.
#if defined(_MSC_VER)
/// @brief Compiler-specific allocation-contract annotation for the benchmark replacement operators.
#define ATLAS_BENCHMARK_ALLOCATION_CONTRACT _Ret_notnull_ _Post_writable_byte_size_(bytes)
/// @brief Compiler-specific allocation-contract annotation for the benchmark replacement operators.
#define ATLAS_BENCHMARK_NOTHROW_CONTRACT                                                           \
    _Ret_maybenull_ _Success_(return != 0) _Post_writable_byte_size_(bytes)
#else
/// @brief Compiler-specific allocation-contract annotation for the benchmark replacement operators.
#define ATLAS_BENCHMARK_ALLOCATION_CONTRACT
/// @brief Compiler-specific allocation-contract annotation for the benchmark replacement operators.
#define ATLAS_BENCHMARK_NOTHROW_CONTRACT
#endif

// Replacement allocators are confined to the benchmark executable, never jobs.
namespace
{
/// @brief Whether the explicitly started benchmark allocation census is active.
std::atomic<bool> enabled{false};
/// @brief Successful allocation requests observed in the current census window.
std::atomic<std::uint64_t> calls{0};
/// @brief Requested allocation bytes observed in the current census window.
std::atomic<std::uint64_t> requested{0};

/// @brief Store a bounded diagnostic observation while preserving loss/error accounting.
void record(std::size_t bytes) noexcept
{
    if (enabled.load(std::memory_order_relaxed))
    {
        calls.fetch_add(1, std::memory_order_relaxed);
        requested.fetch_add(bytes, std::memory_order_relaxed);
    }
}

/// @brief Acquire unaligned backing storage and throw on exhaustion before accounting success.
void* raw_allocate(std::size_t bytes)
{
    while (true)
    {
        if (auto* pointer = std::malloc(std::max<std::size_t>(bytes, 1)))
        {
            return pointer;
        }

        if (auto handler = std::get_new_handler())
        {
            handler();
        }
        else
        {
            throw std::bad_alloc{};
        }
    }
}

/// @brief Count a successful ordinary C++ allocation request after obtaining backing storage.
void* allocate(std::size_t bytes)
{
    auto* pointer = raw_allocate(bytes);
    record(bytes);
    return pointer;
}

/// @brief Acquire correctly aligned backing storage and count the successful requested byte size.
void* allocate_aligned(std::size_t bytes, std::align_val_t requested_alignment)
{
    const auto alignment = std::max(static_cast<std::size_t>(requested_alignment), alignof(void*));
    const auto needed = std::max<std::size_t>(bytes, 1);
    if (alignment - 1 > std::numeric_limits<std::size_t>::max() - sizeof(void*) ||
        needed > std::numeric_limits<std::size_t>::max() - (alignment - 1 + sizeof(void*)))
    {
        throw std::bad_alloc{};
    }

    const auto capacity = needed + alignment - 1 + sizeof(void*);
    auto* original = raw_allocate(capacity);
    auto* pointer = static_cast<void*>(static_cast<std::byte*>(original) + sizeof(void*));
    auto space = capacity - sizeof(void*);
    if (!std::align(alignment, needed, pointer, space))
    {
        std::free(original);
        throw std::bad_alloc{};
    }

    std::memcpy(static_cast<std::byte*>(pointer) - sizeof(void*),
                static_cast<const void*>(&original), sizeof(original));
    record(bytes);
    return pointer;
}

/// @brief Recover and free the backing allocation associated with an aligned pointer.
void release_aligned(void* pointer) noexcept
{
    if (pointer)
    {
        void* original = nullptr;
        std::memcpy(static_cast<void*>(&original), static_cast<std::byte*>(pointer) - sizeof(void*),
                    sizeof(original));
        std::free(original);
    }
}
} // namespace

namespace atlas::jobs::workloads
{
/// @brief Reset and enable the benchmark's successful C++ allocation-request census.
void begin_allocation_counting() noexcept
{
    calls.store(0);
    requested.store(0);
    enabled.store(true);
}

/// @brief Disable allocation counting and return requests/bytes observed in the census window.
allocation_counts end_allocation_counting() noexcept
{
    enabled.store(false);
    return {calls.load(), requested.load()};
}

} // namespace atlas::jobs::workloads

/// @brief Allocate storage and count only successful allocation requests; nothrow overloads return
/// null on failure.
ATLAS_BENCHMARK_ALLOCATION_CONTRACT
void* operator new(std::size_t bytes)
{
    return allocate(bytes);
}

/// @brief Allocate storage and count only successful allocation requests; nothrow overloads return
/// null on failure.
ATLAS_BENCHMARK_ALLOCATION_CONTRACT
void* operator new[](std::size_t bytes)
{
    return allocate(bytes);
}

/// @brief Release the matching allocation without counting deallocation as an allocation request.
void operator delete(void* pointer) noexcept
{
    std::free(pointer);
}

/// @brief Release the matching allocation without counting deallocation as an allocation request.
void operator delete[](void* pointer) noexcept
{
    std::free(pointer);
}

/// @brief Release the matching allocation without counting deallocation as an allocation request.
void operator delete(void* pointer, std::size_t) noexcept
{
    std::free(pointer);
}

/// @brief Release the matching allocation without counting deallocation as an allocation request.
void operator delete[](void* pointer, std::size_t) noexcept
{
    std::free(pointer);
}

/// @brief Allocate aligned storage and count only successful allocation requests; nothrow overloads
/// return null on failure.
ATLAS_BENCHMARK_ALLOCATION_CONTRACT
void* operator new(std::size_t bytes, std::align_val_t alignment)
{
    return allocate_aligned(bytes, alignment);
}

/// @brief Allocate aligned storage and count only successful allocation requests; nothrow overloads
/// return null on failure.
ATLAS_BENCHMARK_ALLOCATION_CONTRACT
void* operator new[](std::size_t bytes, std::align_val_t alignment)
{
    return allocate_aligned(bytes, alignment);
}

/// @brief Release the matching aligned allocation without counting deallocation as an allocation
/// request.
void operator delete(void* pointer, std::align_val_t) noexcept
{
    release_aligned(pointer);
}

/// @brief Release the matching aligned allocation without counting deallocation as an allocation
/// request.
void operator delete[](void* pointer, std::align_val_t) noexcept
{
    release_aligned(pointer);
}

/// @brief Release the matching aligned allocation without counting deallocation as an allocation
/// request.
void operator delete(void* pointer, std::size_t, std::align_val_t) noexcept
{
    release_aligned(pointer);
}

/// @brief Release the matching aligned allocation without counting deallocation as an allocation
/// request.
void operator delete[](void* pointer, std::size_t, std::align_val_t) noexcept
{
    release_aligned(pointer);
}

/// @brief Allocate storage and count only successful allocation requests; nothrow overloads return
/// null on failure.
ATLAS_BENCHMARK_NOTHROW_CONTRACT
void* operator new(std::size_t bytes, const std::nothrow_t&) noexcept
{
    try
    {
        return allocate(bytes);
    }
    catch (...)
    {
        return nullptr;
    }
}

/// @brief Allocate storage and count only successful allocation requests; nothrow overloads return
/// null on failure.
ATLAS_BENCHMARK_NOTHROW_CONTRACT
void* operator new[](std::size_t bytes, const std::nothrow_t&) noexcept
{
    try
    {
        return allocate(bytes);
    }
    catch (...)
    {
        return nullptr;
    }
}

/// @brief Release the matching allocation without counting deallocation as an allocation request.
void operator delete(void* pointer, const std::nothrow_t&) noexcept
{
    std::free(pointer);
}

/// @brief Release the matching allocation without counting deallocation as an allocation request.
void operator delete[](void* pointer, const std::nothrow_t&) noexcept
{
    std::free(pointer);
}

/// @brief Allocate aligned storage and count only successful allocation requests; nothrow overloads
/// return null on failure.
ATLAS_BENCHMARK_NOTHROW_CONTRACT
void* operator new(std::size_t bytes, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    try
    {
        return allocate_aligned(bytes, alignment);
    }
    catch (...)
    {
        return nullptr;
    }
}

/// @brief Allocate aligned storage and count only successful allocation requests; nothrow overloads
/// return null on failure.
ATLAS_BENCHMARK_NOTHROW_CONTRACT
void* operator new[](std::size_t bytes, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    try
    {
        return allocate_aligned(bytes, alignment);
    }
    catch (...)
    {
        return nullptr;
    }
}

/// @brief Release the matching aligned allocation without counting deallocation as an allocation
/// request.
void operator delete(void* pointer, std::align_val_t, const std::nothrow_t&) noexcept
{
    release_aligned(pointer);
}

/// @brief Release the matching aligned allocation without counting deallocation as an allocation
/// request.
void operator delete[](void* pointer, std::align_val_t, const std::nothrow_t&) noexcept
{
    release_aligned(pointer);
}
