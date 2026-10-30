#include "allocation_counter.hpp"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>

// Preserve the CRT's declared contracts when MSVC /analyze checks replacements.
#if defined(_MSC_VER)
#define ATLAS_BENCHMARK_ALLOCATION_CONTRACT _Ret_notnull_ _Post_writable_byte_size_(bytes)
#define ATLAS_BENCHMARK_NOTHROW_CONTRACT                                                           \
    _Ret_maybenull_ _Success_(return != 0) _Post_writable_byte_size_(bytes)
#else
#define ATLAS_BENCHMARK_ALLOCATION_CONTRACT
#define ATLAS_BENCHMARK_NOTHROW_CONTRACT
#endif

// Replacement allocators are confined to the benchmark executable, never jobs.
namespace
{
std::atomic<bool> enabled{false};
std::atomic<std::uint64_t> calls{0};
std::atomic<std::uint64_t> requested{0};
void record(std::size_t bytes) noexcept
{
    if (enabled.load(std::memory_order_relaxed))
    {
        calls.fetch_add(1, std::memory_order_relaxed);
        requested.fetch_add(bytes, std::memory_order_relaxed);
    }
}
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
void* allocate(std::size_t bytes)
{
    auto* pointer = raw_allocate(bytes);
    record(bytes);
    return pointer;
}
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
void begin_allocation_counting() noexcept
{
    calls.store(0);
    requested.store(0);
    enabled.store(true);
}
allocation_counts end_allocation_counting() noexcept
{
    enabled.store(false);
    return {calls.load(), requested.load()};
}

void verify_allocation_counter()
{
    begin_allocation_counting();
    try
    {
        bool aligned = false;
        {
            using pointer = std::unique_ptr<void, void (*)(void*)>;
            pointer ordinary{::operator new(17), [](void* value) { ::operator delete(value); }};
            pointer array{::operator new[](19), [](void* value) { ::operator delete[](value); }};
            pointer wide{::operator new(64, std::align_val_t{64}),
                         [](void* value) { ::operator delete(value, std::align_val_t{64}); }};
            pointer optional{::operator new(64, std::align_val_t{64}, std::nothrow),
                             [](void* value) { ::operator delete(value, std::align_val_t{64}); }};
            aligned = optional && reinterpret_cast<std::uintptr_t>(wide.get()) % 64 == 0 &&
                      reinterpret_cast<std::uintptr_t>(optional.get()) % 64 == 0;
        }
        const auto result = end_allocation_counting();
        if (!aligned || result.calls != 4 || result.requested_bytes != 164)
        {
            throw std::runtime_error{"Benchmark allocation-counter self-check failed"};
        }
    }
    catch (...)
    {
        (void)end_allocation_counting();
        throw;
    }
}
} // namespace atlas::jobs::workloads

ATLAS_BENCHMARK_ALLOCATION_CONTRACT
void* operator new(std::size_t bytes)
{
    return allocate(bytes);
}
ATLAS_BENCHMARK_ALLOCATION_CONTRACT
void* operator new[](std::size_t bytes)
{
    return allocate(bytes);
}
void operator delete(void* pointer) noexcept
{
    std::free(pointer);
}
void operator delete[](void* pointer) noexcept
{
    std::free(pointer);
}
void operator delete(void* pointer, std::size_t) noexcept
{
    std::free(pointer);
}
void operator delete[](void* pointer, std::size_t) noexcept
{
    std::free(pointer);
}
ATLAS_BENCHMARK_ALLOCATION_CONTRACT
void* operator new(std::size_t bytes, std::align_val_t alignment)
{
    return allocate_aligned(bytes, alignment);
}
ATLAS_BENCHMARK_ALLOCATION_CONTRACT
void* operator new[](std::size_t bytes, std::align_val_t alignment)
{
    return allocate_aligned(bytes, alignment);
}
void operator delete(void* pointer, std::align_val_t) noexcept
{
    release_aligned(pointer);
}
void operator delete[](void* pointer, std::align_val_t) noexcept
{
    release_aligned(pointer);
}
void operator delete(void* pointer, std::size_t, std::align_val_t) noexcept
{
    release_aligned(pointer);
}
void operator delete[](void* pointer, std::size_t, std::align_val_t) noexcept
{
    release_aligned(pointer);
}
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
void operator delete(void* pointer, const std::nothrow_t&) noexcept
{
    std::free(pointer);
}
void operator delete[](void* pointer, const std::nothrow_t&) noexcept
{
    std::free(pointer);
}
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
void operator delete(void* pointer, std::align_val_t, const std::nothrow_t&) noexcept
{
    release_aligned(pointer);
}
void operator delete[](void* pointer, std::align_val_t, const std::nothrow_t&) noexcept
{
    release_aligned(pointer);
}
