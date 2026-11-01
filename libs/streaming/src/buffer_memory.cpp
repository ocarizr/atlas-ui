/** @file
 * @brief Exception-safe byte reservations and replaceable aligned payload allocation.
 */
#include "buffer_memory.hpp"
#include <algorithm>
#include <stdexcept>

namespace atlas::streaming::detail
{
/// @brief Retain the caller's resource or select the process-lifetime new/delete allocator.
buffer_memory::buffer_memory(std::shared_ptr<std::pmr::memory_resource> upstream)
    : upstream_{std::move(upstream)}
{
    if (!upstream_)
    {
        upstream_ = {std::pmr::new_delete_resource(), [](std::pmr::memory_resource*)
                     {
                     }};
    }
}

/// @brief Reserve exact output bytes atomically without permitting quota overflow.
void buffer_memory::reserve(std::size_t size, std::size_t limit)
{
    auto current = resident.load();
    do
    {
        if (size > limit - current)
        {
            throw std::overflow_error{"The asset streamer byte budget is exhausted"};
        }
    } while (!resident.compare_exchange_weak(current, current + size));

    auto maximum = peak.load();
    while (maximum < current + size && !peak.compare_exchange_weak(maximum, current + size))
    {
    }
}

/// @brief Return the bytes charged to one completed reservation lifetime.
void buffer_memory::release(std::size_t size) noexcept
{
    resident.fetch_sub(size);
}

/// @brief Forward allocation with at least 64-byte alignment for disjoint copy boundaries.
void* buffer_memory::do_allocate(std::size_t bytes, std::size_t alignment)
{
    return upstream_->allocate(bytes, std::max(alignment, std::size_t{64}));
}

/// @brief Match the alignment forwarded during allocation.
void buffer_memory::do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment)
{
    upstream_->deallocate(pointer, bytes, std::max(alignment, std::size_t{64}));
}

/// @brief Distinct adapter instances never share allocator identity.
bool buffer_memory::do_is_equal(const std::pmr::memory_resource& other) const noexcept
{
    return this == &other;
}

/// @brief Last read/output owner releases the reservation without retaining the streamer itself.
buffer_reservation::~buffer_reservation()
{
    if (memory)
    {
        memory->release(size);
    }
}

/// @brief Allocate reservation metadata before changing shared quota counters.
std::shared_ptr<buffer_reservation> reserve_buffer(const std::shared_ptr<buffer_memory>& memory,
                                                   std::size_t size, std::size_t limit)
{
    auto reservation = std::make_shared<buffer_reservation>();
    memory->reserve(size, limit);
    reservation->memory = memory;
    reservation->size = size;
    return reservation;
}

/// @brief Keep allocator and quota ownership in the output deleter, including construction failure
/// cleanup.
std::shared_ptr<asset_bytes> allocate_buffer(const std::shared_ptr<buffer_reservation>& reservation)
{
    auto output =
        std::shared_ptr<asset_bytes>{new asset_bytes(reservation->size, reservation->memory.get()),
                                     [owner = reservation](asset_bytes* bytes) mutable
                                     {
                                         delete bytes;
                                         owner.reset();
                                     }};
    return output;
}
} // namespace atlas::streaming::detail
