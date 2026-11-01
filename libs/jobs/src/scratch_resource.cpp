/**
 * @file
 * @brief Synchronized task-local monotonic storage with explicit upstream ownership.
 */

#include "scratch_resource.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>

namespace atlas::jobs::detail
{
/// @brief Initialize task-owned synchronized monotonic resource with bounded reserved storage and
/// bulk reclamation.
scratch_resource::scratch_resource(scratch_options options) : options_{options}
{
    if (!options.block_size || !options.max_reserved_bytes || !options.upstream)
    {
        throw std::invalid_argument{
            "Scratch needs positive block/budget sizes and a live upstream"};
    }
}

/// @brief Return the block to its original upstream with the recorded size/alignment; terminate if
/// the upstream violates no-throw cleanup.
void scratch_resource::block_deleter::operator()(std::byte* pointer) const noexcept
{
    try
    {
        upstream->deallocate(pointer, size, alignment);
    }
    catch (...)
    {
        std::terminate();
    }
}

/// @brief Reserve an aligned scratch block within the configured per-task byte budget.
void scratch_resource::add_block(std::size_t bytes, std::align_val_t requested_alignment)
{
    const auto remaining = options_.max_reserved_bytes - statistics_.reserved_bytes;
    if (bytes > remaining)
    {
        throw std::bad_alloc{};
    }

    const auto size = std::min(std::max(options_.block_size, bytes), remaining);
    const auto alignment =
        std::max(static_cast<std::size_t>(requested_alignment), alignof(std::max_align_t));
    auto* pointer = static_cast<std::byte*>(options_.upstream->allocate(size, alignment));
    block pending{{pointer, block_deleter{options_.upstream, size, alignment}}, size};
    blocks_.push_back(std::move(pending));
    statistics_.reserved_bytes += size;
    ++statistics_.block_count;
}

/// Standard memory_resource override fixes the bytes/alignment parameter order.
/// @brief Allocate aligned temporary storage from this task's synchronized monotonic blocks.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void* scratch_resource::do_allocate(std::size_t bytes, std::size_t alignment)
{
    if (!std::has_single_bit(alignment))
    {
        throw std::invalid_argument{"Invalid scratch alignment"};
    }

    const auto needed = std::max<std::size_t>(bytes, 1);
    const std::lock_guard lock{mutex_};
    if (bytes > std::numeric_limits<std::size_t>::max() - statistics_.requested_bytes)
    {
        throw std::bad_alloc{};
    }

    void* pointer = nullptr;
    std::size_t space = 0;
    if (!blocks_.empty())
    {
        auto& last = blocks_.back();
        pointer = last.data.get() + last.used;
        space = last.size - last.used;
    }

    if (!pointer || !std::align(alignment, needed, pointer, space))
    {
        add_block(needed, static_cast<std::align_val_t>(alignment));
        pointer = blocks_.back().data.get();
    }

    auto& last = blocks_.back();
    last.used =
        static_cast<std::size_t>(static_cast<std::byte*>(pointer) - last.data.get()) + needed;
    ++statistics_.allocation_calls;
    statistics_.requested_bytes += bytes;
    return pointer;
}

/// @brief Return an observational snapshot of this instance's counters without resetting them.
scratch_statistics scratch_resource::statistics() const
{
    const std::lock_guard lock{mutex_};
    return statistics_;
}
} // namespace atlas::jobs::detail
