/** @file
 * @brief Shared payload allocator and retained-output byte accounting.
 */
#pragma once
#include <atlas/streaming/asset_streamer.hpp>
#include <atomic>

namespace atlas::streaming::detail
{
/// @brief Shared counters and cache-aligned adapter over an injected thread-safe allocator.
class buffer_memory final : public std::pmr::memory_resource
{
  public:
    /// @brief Own the upstream payload resource independently of the streamer lifetime.
    explicit buffer_memory(std::shared_ptr<std::pmr::memory_resource> upstream);
    /// @brief Reserve bytes before admission; throw without changing the counter on exhaustion.
    void reserve(std::size_t size, std::size_t limit);
    /// @brief Release one reservation when its last operation/output owner disappears.
    void release(std::size_t size) noexcept;
    /// @brief Concurrently reserved output bytes.
    std::atomic<std::size_t> resident{0};
    /// @brief Highest observed byte reservation.
    std::atomic<std::size_t> peak{0};

  private:
    /// @brief Allocate payload storage aligned to at least the copy-grain cache-line boundary.
    void* do_allocate(std::size_t bytes, std::size_t alignment) override;
    /// @brief Return storage with the matching forwarded size and alignment.
    void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override;
    /// @brief Allocation adapters compare by identity.
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override;
    /// @brief Owned upstream resource, or a borrowed process-lifetime new/delete resource.
    std::shared_ptr<std::pmr::memory_resource> upstream_;
};

/// @brief Reservation lifetime shared by the accepted read and its output buffer.
struct buffer_reservation
{
    /// @brief Release the charged bytes after both read and retained-output ownership end.
    ~buffer_reservation();
    /// @brief Shared allocator/counters retained beyond streamer destruction if necessary.
    std::shared_ptr<buffer_memory> memory;
    /// @brief Exact output bytes charged by this read.
    std::size_t size{0};
};

/// @brief Reserve output bytes with exception-safe metadata allocation.
std::shared_ptr<buffer_reservation> reserve_buffer(const std::shared_ptr<buffer_memory>& memory,
                                                   std::size_t size, std::size_t limit);
/// @brief Allocate one exact-size vector, retaining its allocator and byte reservation.
std::shared_ptr<asset_bytes>
allocate_buffer(const std::shared_ptr<buffer_reservation>& reservation);
} // namespace atlas::streaming::detail
