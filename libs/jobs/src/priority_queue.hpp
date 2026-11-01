/**
 * @file
 * @brief Weighted FIFO eligibility dispatch without worker-side prerequisite waits.
 */

#pragma once

#include "graph_state.hpp"
#include <atlas/jobs/job_policy.hpp>

#include <array>

namespace atlas::jobs::detail
{
/// Pool-mutex protected, allocation-free admission/selection of prepared entries.
class priority_queue
{
  public:
    /// @brief Borrow the normal-priority queue for legacy scheduling compatibility.
    [[nodiscard]] ready_queue& normal() noexcept
    {
        return queues_[1];
    }

    /// @brief Splice prepared work into the selected weighted priority queue.
    void push(ready_queue& entries, job_priority priority) noexcept
    {
        auto& destination = queues_[static_cast<std::size_t>(priority)];
        destination.splice(destination.end(), entries);
    }

    /// @brief Return current ready-entry counts indexed by high/normal/background priority.
    [[nodiscard]] std::array<std::size_t, 3> sizes() const noexcept
    {
        return {queues_[0].size(), queues_[1].size(), queues_[2].size()};
    }

    /// @brief Parse a representable unsigned capacity without accepting negative or malformed text.
    [[nodiscard]] std::size_t size() const noexcept
    {
        return queues_[0].size() + queues_[1].size() + queues_[2].size();
    }

    /// @brief Check whether this queue set currently holds any ready work.
    [[nodiscard]] bool empty() const noexcept
    {
        return queues_[0].empty() && queues_[1].empty() && queues_[2].empty();
    }

    /// @brief Select the next ready queue using weighted service credits without starving a ready
    /// class.
    void take(ready_queue& destination) noexcept
    {
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            for (std::size_t index = 0; index < queues_.size(); ++index)
            {
                auto& source = queues_[index];
                if (credits_[index] && !source.empty())
                {
                    --credits_[index];
                    ++dispatched[index];
                    ++dispatch_count;
                    destination.splice(destination.end(), source, source.begin());
                    return;
                }
            }

            credits_ = weights;
        }

        std::terminate(); // take() requires at least one ready entry.
    }

    /// @brief Weighted dispatch counters retained for diagnostics.
    std::array<std::size_t, 3> dispatched{};
    /// @brief Runtime-local ready dispatch sequence, independent of queue-class service weights.
    std::uint64_t dispatch_count{0};

  private:
    /// @brief High/normal/background service weights used by the shared FIFO dispatch policy.
    static constexpr std::array<std::size_t, 3> weights{4, 2, 1};
    /// @brief Remaining weighted service credits for the current dispatch round.
    std::array<std::size_t, 3> credits_{weights};
    /// @brief FIFO ready queues indexed by scheduling class.
    std::array<ready_queue, 3> queues_;
};
} // namespace atlas::jobs::detail
