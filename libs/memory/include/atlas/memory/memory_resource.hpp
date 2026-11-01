/**
 * @file
 * @brief Application-owned, concurrent PMR allocation shared by reusable libraries.
 */
#pragma once
#include <atomic>
#include <cstddef>
#include <memory>
#include <memory_resource>
#include <string_view>

namespace atlas::memory
{
/// @brief Optional observation policy; disabled counters avoid shared atomic updates per
/// allocation.
struct resource_options
{
    /// @brief Record requested-byte ownership, not allocator caches or process working set.
    bool collect_statistics{false};
};

/// @brief Independently sampled counters for allocations routed through this resource only.
struct allocation_statistics
{
    /// @brief Successful allocation calls, including zero-byte requests.
    std::size_t allocation_calls{0};
    /// @brief Matching deallocations performed through this resource.
    std::size_t deallocation_calls{0};
    /// @brief Requested bytes still owned by callers.
    std::size_t live_requested_bytes{0};
    /// @brief Maximum observed sum of requested live bytes.
    std::size_t peak_requested_bytes{0};
};

/// @brief Thread-safe adapter over the configured allocator, without global allocation replacement.
///
/// Resource owners must outlive every borrowing PMR container and job scratch scope.
/// Shared consumers such as the streamer can retain ownership beyond application service teardown.
/// Allocations may be released by another thread, with their original size/alignment.
class memory_resource final : public std::pmr::memory_resource
{
  public:
    /// @brief Select observation policy; backend selection belongs to the build/composition policy.
    explicit memory_resource(resource_options options = {});
    /// @brief Identify the actual backend selected for this build, including the ASan fallback.
    [[nodiscard]] std::string_view backend_name() const noexcept;
    /// @brief Snapshot optional counters; all fields stay zero when collection is disabled.
    [[nodiscard]] allocation_statistics statistics() const noexcept;

  private:
    /// @brief Allocate aligned bytes or throw; rejected/failed allocations do not change counters.
    void* do_allocate(std::size_t bytes, std::size_t alignment) override;
    /// @brief Release an allocation using the same backend and update enabled accounting.
    void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override;
    /// @brief Preserve independent accounting/lifetimes by comparing resource identity.
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override;
    /// @brief Immutable instrumentation policy.
    const resource_options options_;
    /// @brief Successful allocations routed through this adapter.
    std::atomic<std::size_t> allocations_{0};
    /// @brief Matching releases routed through this adapter.
    std::atomic<std::size_t> deallocations_{0};
    /// @brief Requested bytes currently owned by callers.
    std::atomic<std::size_t> live_{0};
    /// @brief Requested-byte high-water mark.
    std::atomic<std::size_t> peak_{0};
};

/// @brief Create a shared allocator owner usable by any PMR-aware subsystem.
[[nodiscard]] std::shared_ptr<memory_resource> make_resource(resource_options options = {});
} // namespace atlas::memory
