/** @file @brief Deterministic partitioning with a sequential small-frame path. */
#include "recording_plan.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace atlas::renderer::detail
{
/// @brief Partition frame order while charging clear-only passes one scheduling unit.
std::vector<recording_group> plan_recording(std::span<const std::size_t> draw_counts,
                                            recording_options options)
{
    options.validate();
    if (draw_counts.empty())
    {
        throw std::invalid_argument("recording requires a nonempty frame");
    }

    std::size_t draws = 0;
    std::size_t units = 0;
    for (const auto count : draw_counts)
    {
        const auto charge = std::max(std::size_t{1}, count);
        if (charge > std::numeric_limits<std::size_t>::max() - units)
        {
            throw std::length_error("recording workload overflow");
        }

        units += charge;
        draws += count;
    }

    const auto count = std::clamp(draws / options.min_draws_per_batch, std::size_t{1},
                                  std::size_t{options.max_batches});
    auto result = std::vector<recording_group>(count);
    std::size_t group = 0;
    std::size_t remaining = units / count + (units % count > 0 ? 1 : 0);
    for (std::size_t pass = 0; pass < draw_counts.size(); ++pass)
    {
        const auto total = std::max(std::size_t{1}, draw_counts[pass]);
        for (std::size_t begin = 0; begin < total;)
        {
            const auto take = std::min(total - begin, remaining);
            result[group].push_back({pass, begin, draw_counts[pass] == 0 ? 0 : take, begin == 0,
                                     begin + take == total});
            begin += take;
            remaining -= take;
            if (remaining == 0 && group + 1 < count)
            {
                ++group;
                remaining = units / count + (group < units % count ? 1 : 0);
            }
        }
    }

    return result;
}
} // namespace atlas::renderer::detail
