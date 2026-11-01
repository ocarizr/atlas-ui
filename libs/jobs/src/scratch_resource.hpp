/**
 * @file
 * @brief Synchronized task-local monotonic storage with explicit upstream ownership.
 */

#pragma once

#include <atlas/jobs/job_memory.hpp>

#include <memory>
#include <mutex>
#include <vector>

namespace atlas::jobs::detail
{
/// @brief Task-owned synchronized monotonic resource with bounded reserved storage and bulk
/// reclamation.
class scratch_resource final : public std::pmr::memory_resource
{
  public:
    /// @brief Initialize task-owned synchronized monotonic resource with bounded reserved storage
    /// and bulk reclamation.
    explicit scratch_resource(scratch_options options);
    /// @brief Return an observational snapshot of this instance's counters without resetting them.
    scratch_statistics statistics() const;

  private:
    /// @brief Upstream-aware deleter preserving the block's byte size and alignment at release.
    struct block_deleter
    {
        /// @brief Borrowed upstream allocator; it must outlive allocations and support concurrent
        /// scopes.
        std::pmr::memory_resource* upstream;
        /// @brief Exact requested byte count or allocation extent in the enclosing record.
        std::size_t size;
        /// @brief Alignment retained for correct upstream allocation/release.
        std::size_t alignment;
        /// @brief Return the block to its original upstream with the recorded size/alignment;
        /// terminate if the upstream violates no-throw cleanup.
        void operator()(std::byte* pointer) const noexcept;
    };

    /// @brief One owned scratch block and its allocation cursor; individual frees do not reclaim
    /// it.
    struct block
    {
        /// @brief Owned scratch block backing storage with its original upstream-aware deleter.
        std::unique_ptr<std::byte, block_deleter> data;
        /// @brief Exact requested byte count or allocation extent in the enclosing record.
        std::size_t size;
        /// @brief Byte cursor already allocated within this scratch block.
        std::size_t used{0};
    };

    /// @brief Allocate aligned temporary storage from this task's synchronized monotonic blocks.
    void* do_allocate(std::size_t bytes, std::size_t alignment) override;

    /// @brief Leave individual monotonic allocations for bulk scope reclamation.
    void do_deallocate(void*, std::size_t, std::size_t) override
    {
    }

    /// @brief Compare memory-resource identity; distinct resources are not interchangeable.
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }

    /// @brief Reserve an aligned scratch block within the configured per-task byte budget.
    void add_block(std::size_t bytes, std::align_val_t alignment);
    /// @brief Immutable policy/configuration retained by this object.
    scratch_options options_;
    /// @brief Synchronization for this object's shared mutable storage.
    mutable std::mutex mutex_;
    /// @brief Owned monotonic scratch blocks reclaimed together at scope completion.
    std::vector<block> blocks_;
    /// @brief Successful allocation/high-water statistics for this resource.
    scratch_statistics statistics_;
};
} // namespace atlas::jobs::detail
