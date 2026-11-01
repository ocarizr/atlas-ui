/**
 * @file
 * @brief Sanity checks for the benchmark replacement allocation entry points.
 */

#include "allocation_counter.hpp"

#include <memory>
#include <new>
#include <stdexcept>

namespace atlas::jobs::workloads
{
/// Exercise the public allocation operators independently of their replacements.
void verify_allocation_counter()
{
    begin_allocation_counting();
    try
    {
        bool aligned = false;
        {
            const auto release = [](void* value)
            {
                ::operator delete(value);
            };
            const auto release_array = [](void* value)
            {
                ::operator delete[](value);
            };
            const auto release_aligned = [](void* value)
            {
                ::operator delete(value, std::align_val_t{64});
            };
            std::unique_ptr<void, decltype(release)> ordinary{::operator new(17), release};
            std::unique_ptr<void, decltype(release_array)> array{::operator new[](19),
                                                                 release_array};
            std::unique_ptr<void, decltype(release_aligned)> wide{
                ::operator new(64, std::align_val_t{64}), release_aligned};
            std::unique_ptr<void, decltype(release_aligned)> optional{
                ::operator new(64, std::align_val_t{64}, std::nothrow), release_aligned};
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
