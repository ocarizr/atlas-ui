/** @file @brief Repeatable allocation churn probe; timings are observations, never speed
 * assertions. */
#include <algorithm>
#include <array>
#include <atlas/memory/memory_resource.hpp>
#include <chrono>
#include <cstdio>
#include <exception>
#include <memory_resource>
#include <stdexcept>
#include <string_view>

namespace
{
/// @brief Release every live slot even if an allocation fails partway through a batch.
struct allocation_batch
{
    /// @brief Borrow the resource while retaining the exact deallocation parameters.
    allocation_batch(std::pmr::memory_resource& resource, std::size_t bytes)
        : resource{resource}, bytes{bytes}
    {
    }

    /// @brief Allocation ownership cannot be duplicated between batches.
    allocation_batch(const allocation_batch&) = delete;
    /// @brief Prevent replacing live allocations with another batch's pointer array.
    allocation_batch& operator=(const allocation_batch&) = delete;

    /// @brief Paired deallocation never depends on allocation order or a successful full batch.
    ~allocation_batch()
    {
        for (auto* pointer : pointers)
        {
            if (pointer)
            {
                resource.deallocate(pointer, bytes, 64);
            }
        }
    }

    /// @brief Allocator under observation; caller retains its lifetime.
    std::pmr::memory_resource& resource;
    /// @brief Exact payload bytes per allocation.
    std::size_t bytes;
    /// @brief Fixed metadata avoids using the measured allocator for the benchmark harness.
    std::array<void*, 16> pointers{};
};

/// @brief Explicit sample dimensions keep allocation size separate from repetition count.
struct sample_options
{
    /// @brief Requested bytes per allocation.
    std::size_t bytes;
    /// @brief Number of complete allocate/free batches.
    std::size_t rounds;
};

/// @brief Touch endpoints to validate usable storage without measuring full-buffer initialization.
double sample(std::pmr::memory_resource& resource, sample_options options)
{
    const auto [bytes, rounds] = options;
    std::size_t checksum{0};
    const auto start = std::chrono::steady_clock::now();
    for (std::size_t round = 0; round < rounds; ++round)
    {
        allocation_batch batch{resource, bytes};
        for (auto& pointer : batch.pointers)
        {
            pointer = resource.allocate(bytes, 64);
            auto* data = static_cast<unsigned char*>(pointer);
            data[0] = 17;
            data[bytes - 1] = 25;
            checksum += data[0] + data[bytes - 1];
        }
    }

    const auto elapsed = std::chrono::steady_clock::now() - start;
    if (checksum != rounds * 16 * 42)
    {
        throw std::runtime_error{"Allocation probe checksum mismatch"};
    }

    return std::chrono::duration<double, std::nano>{elapsed}.count() /
           static_cast<double>(rounds * 16);
}

/// @brief Report median batch-allocation/free latency after warmup for this particular process/run.
void report(std::string_view name, std::pmr::memory_resource& resource, bool smoke)
{
    for (const auto size :
         {std::size_t{64}, std::size_t{4096}, std::size_t{65536}, std::size_t{4} * 1024 * 1024})
    {
        (void)sample(resource, {.bytes = size, .rounds = 2});
        std::array<double, 5> samples{};
        for (auto& timing : samples)
        {
            timing = sample(resource, {.bytes = size, .rounds = smoke ? 1U : 64U});
        }

        std::sort(samples.begin(), samples.end());
        std::printf("%.*s,%zu,%.1f\n", static_cast<int>(name.size()), name.data(), size,
                    samples[2]);
    }
}
} // namespace

/// @brief Compare the system baseline and configured PMR resource without changing global heaps.
int main(int argc, char** argv)
{
    const bool smoke = argc == 2 && std::string_view{argv[1]} == "--smoke";
    if (argc != 1 && !smoke)
    {
        return 2;
    }

    try
    {
        const auto configured = atlas::memory::make_resource();
        std::puts("backend,bytes,median_ns_per_allocation_and_free");
        report("system", *std::pmr::new_delete_resource(), smoke);
        report(configured->backend_name(), *configured, smoke);
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }

    return 0;
}
