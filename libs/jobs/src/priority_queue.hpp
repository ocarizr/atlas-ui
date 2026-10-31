#pragma once

#include "graph_state.hpp"
#include <atlas/jobs/job_policy.hpp>

#include <array>

namespace atlas::jobs::detail
{
// Pool-mutex protected, allocation-free admission/selection of prepared entries.
class priority_queue
{
  public:
    [[nodiscard]] ready_queue& normal() noexcept
    {
        return queues_[1];
    }
    void push(ready_queue& entries, job_priority priority) noexcept
    {
        auto& destination = queues_[static_cast<std::size_t>(priority)];
        destination.splice(destination.end(), entries);
    }
    [[nodiscard]] std::array<std::size_t, 3> sizes() const noexcept
    {
        return {queues_[0].size(), queues_[1].size(), queues_[2].size()};
    }
    [[nodiscard]] std::size_t size() const noexcept
    {
        return queues_[0].size() + queues_[1].size() + queues_[2].size();
    }
    [[nodiscard]] bool empty() const noexcept
    {
        return queues_[0].empty() && queues_[1].empty() && queues_[2].empty();
    }
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
    std::array<std::size_t, 3> dispatched{};
    std::uint64_t dispatch_count{0};

  private:
    static constexpr std::array<std::size_t, 3> weights{4, 2, 1};
    std::array<std::size_t, 3> credits_{weights};
    std::array<ready_queue, 3> queues_;
};
} // namespace atlas::jobs::detail
