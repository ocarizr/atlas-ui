/** @file @brief Owner-only mutable history and allocator-retained immutable batch storage. */
#pragma once
#include <atlas/ui/commands.hpp>
#include <thread>
#include <vector>

namespace atlas::ui
{
/// @brief Every vector is destroyed before its shared allocation resource.
class detail::command_data
{
  public:
    command_data(std::uint64_t frame_id, std::shared_ptr<std::pmr::memory_resource> resource,
                 std::uint32_t batch_id)
        : memory(std::move(resource)), frame(frame_id), ordinal(batch_id), writes(0, memory.get()),
          conflicts(0, memory.get())
    {
    } ///< Counted empty vectors propagate debug iterator metadata allocation failures.

    std::shared_ptr<std::pmr::memory_resource> memory; ///< Keeps caller allocation policy alive.
    std::uint64_t frame;                               ///< Explicit session frame identity.
    std::uint32_t ordinal;                             ///< Sync-boundary ordinal.
    std::pmr::vector<property_write> writes; ///< One current-batch winner per addressed key.
    std::pmr::vector<command_conflict>
        conflicts; ///< One current-batch diagnostic per repeated key.
};

/// @brief Serial production reserves once; sealing prepares new data before committing its cursor.
class command_buffer::impl
{
  public:
    impl(command_limits policy, std::shared_ptr<std::pmr::memory_resource> resource,
         diagnostics::services services)
        : limits(policy), memory(std::move(resource)), destinations(std::move(services)),
          history(0, memory.get())
    {
        history.reserve(limits.max_commands);
    } ///< Counted construction keeps debug metadata allocation outside a noexcept constructor.

    void check(bool need_frame = true) const
    {
        if (owner != std::this_thread::get_id())
        {
            throw std::logic_error("UI commands require their owner thread");
        }

        if (sealing)
        {
            throw std::logic_error("UI command sealing cannot be reentered");
        }

        if (need_frame && frame == 0)
        {
            throw std::logic_error("UI commands require begin_frame");
        }
    } ///< Check affinity before reading any mutable producer state.

    command_limits limits;                             ///< Immutable validated admission policy.
    std::shared_ptr<std::pmr::memory_resource> memory; ///< Outlives producer storage and batches.
    diagnostics::services destinations; ///< Optional library logging/profiling endpoints.
    const std::thread::id owner = std::this_thread::get_id(); ///< Sole admission coordinator.
    std::pmr::vector<property_write> history; ///< Bounded accepted writes for the entire frame.
    std::uint64_t frame = 0;                  ///< Zero until the first explicit frame begins.
    std::uint32_t ordinal = 0;                ///< Last successfully sealed batch in this frame.
    std::size_t sealed = 0; ///< History prefix already published; never retroactively edited.
    bool sealing = false;   ///< Blocks synchronous diagnostic callbacks from mutating this state.
};
} // namespace atlas::ui
