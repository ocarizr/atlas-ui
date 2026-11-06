/** @file @brief Reusable PMR-only allocation census for MVVM tests and workload probes. */
#pragma once
#include <algorithm>
#include <memory_resource>

namespace atlas::mvvm::benchmarks
{
/// @brief Counts library PMR payloads, excluding schema metadata and arbitrary client allocations.
class counting_resource final : public std::pmr::memory_resource
{
  public:
    std::size_t allocations = 0; ///< Successful upstream allocations.
    std::size_t live_bytes = 0;  ///< Outstanding PMR bytes.
    std::size_t peak_bytes = 0;  ///< Peak outstanding PMR bytes.
    bool reject = false;         ///< Deterministic failure injection after setup.
  private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override
    {
        if (reject)
        {
            throw std::bad_alloc();
        }

        auto* result = std::pmr::new_delete_resource()->allocate(bytes, alignment);
        ++allocations;
        live_bytes += bytes;
        peak_bytes = std::max(peak_bytes, live_bytes);
        return result;
    } ///< Count successful admission without intercepting global allocation.

    void do_deallocate(void* address, std::size_t bytes, std::size_t alignment) override
    {
        live_bytes -= bytes;
        std::pmr::new_delete_resource()->deallocate(address, bytes, alignment);
    } ///< Every retained PMR allocation must retire before resource destruction.

    /// @brief Compare allocator ownership by resource identity.
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }

    ///< Resource identity defines allocation ownership.
};
} // namespace atlas::mvvm::benchmarks
