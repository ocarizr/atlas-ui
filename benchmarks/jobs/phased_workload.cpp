/**
 * @file
 * @brief Seeded map/fold/ordered-merge workloads for generic scheduler validation.
 */

#include "phased_workload.hpp"

#include <algorithm>
#include <memory_resource>

namespace atlas::jobs::workloads
{
namespace
{
/// @brief Fixed ordered-fold multiplier for the reproducible unsigned checksum.
constexpr std::uint64_t multiplier = 0x100000001b3ULL;

/// @brief Explicit transform iteration count, avoiding accidental interchange with item values.
struct round_count
{
    /// @brief Current numeric observation or explicit scenario/workload value, according to its
    /// enclosing type.
    std::size_t value;
};

/// @brief Compute the deterministic per-item work used by the synthetic reference workload.
std::uint64_t transform(std::uint64_t value, round_count rounds)
{
    for (std::size_t round = 0; round < rounds.value; ++round)
    {
        value += 0x9e3779b97f4a7c15ULL;
        value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
        value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
        value ^= value >> 31;
    }

    return value;
}

/// @brief Compute the ordered-fold multiplier needed to combine partial hashes deterministically.
std::uint64_t power(std::size_t exponent)
{
    std::uint64_t result = 1;
    auto base = multiplier;
    while (exponent != 0)
    {
        if ((exponent & 1U) != 0)
        {
            result *= base;
        }

        base *= base;
        exponent >>= 1;
    }

    return result;
}

/// @brief Visit one disjoint input span, recomputing dirty elements and counting all visited
/// elements.
void map_chunk(job_context&, std::span<work_item> items)
{
    auto& state = *items.front().owner;
    if (state.phase.load() != 1)
    {
        throw std::logic_error{"Map escaped its owning phase"};
    }

    std::size_t computed = 0;
    for (auto& item : items)
    {
        if (item.index == state.config.fail_index)
        {
            throw std::runtime_error{"Injected map failure"};
        }

        if (item.dirty)
        {
            item.output = transform(item.input, {state.config.rounds});
            ++computed;
        }
    }

    state.computed.fetch_add(computed);
    state.mapped.fetch_add(items.size());
}

/// @brief Advance the batch into its map phase and dispatch disjoint chunks; only dirty elements
/// are transformed inside each chunk.
void map_stage(job_context& context, std::shared_ptr<workload_state>& state)
{
    int initial = 0;
    if (!state->phase.compare_exchange_strong(initial, 1))
    {
        throw std::logic_error{"A workload batch cannot be reopened"};
    }

    context.parallel_for("map range", std::span{state->items}, state->config.grain, map_chunk);
}

/// @brief Owned/borrowed state required by one scratch-backed range-fold stage.
struct fold_input
{
    /// @brief Borrowed immutable mapped items; the parent workload scope pins their storage.
    std::span<const work_item> items;
    /// @brief Borrowed destination partial result pinned by the parent workload scope.
    partial_result* output;
};

/// @brief Compute a scratch-backed partial hash for one deterministic range.
void fold_chunk(job_context& context, fold_input& data)
{
    auto access =
        context.access({read_access(data.items), write_access(std::span{data.output, 1})});
    std::pmr::vector<std::uint64_t> temporary{&context.scratch()};
    temporary.reserve(data.items.size());
    for (const auto& item : data.items)
    {
        temporary.push_back(item.output);
    }

    std::uint64_t hash = 0;
    for (const auto value : temporary)
    {
        hash = hash * multiplier + value;
    }

    *data.output = {hash, temporary.size()};
    data.items.front().owner->folded.fetch_add(data.items.size());
}

/// @brief Schedule partial folding after mapping has completed.
void fold_stage(job_context& context, std::shared_ptr<workload_state>& state)
{
    if (state->mapped.load() != state->items.size())
    {
        throw std::logic_error{"Fold started before map descendants completed"};
    }

    state->phase.store(2);
    const std::span<const work_item> items{state->items};
    for (std::size_t index = 0; index < state->partials.size(); ++index)
    {
        const auto start = index * state->config.grain;
        const auto count = std::min(state->config.grain, items.size() - start);
        context.spawn("fold [" + std::to_string(index) + "]",
                      fold_input{items.subspan(start, count), &state->partials[index]}, fold_chunk);
    }
}

/// @brief Merge partial results in stable range order and publish the final checksum.
void merge_stage(job_context&, std::shared_ptr<workload_state>& state)
{
    if (state->folded.load() != state->items.size())
    {
        throw std::logic_error{"Merge started before fold descendants completed"};
    }

    for (const auto& partial : state->partials)
    {
        state->checksum = state->checksum * power(partial.length) + partial.hash;
    }

    state->phase.store(3);
}
} // namespace

/// @brief Build a reproducible seeded input snapshot and incremental cache state.
std::shared_ptr<workload_state> prepare_workload(workload_config config)
{
    if (config.grain == 0 || config.rounds == 0)
    {
        throw std::invalid_argument{"A workload needs positive grain and rounds"};
    }

    auto state = std::make_shared<workload_state>();
    state->config = config;
    state->items.reserve(config.size);
    for (std::size_t index = 0; index < config.size; ++index)
    {
        auto input = transform(config.seed + index, {1});
        const auto cached = config.dirty_stride == 1 ? 0 : transform(input, {config.rounds});
        const bool dirty = config.dirty_stride != 0 && index % config.dirty_stride == 0;
        if (config.dirty_stride > 1 && dirty)
        {
            input ^= 0x9e3779b97f4a7c15ULL;
        }

        state->items.push_back({input, cached, state.get(), index, dirty});
    }

    state->partials.resize(config.size == 0 ? 0 : 1 + (config.size - 1) / config.grain);
    return state;
}

/// @brief Compute the sequential oracle for the same snapshot, rounds and ordering.
std::uint64_t sequential_reference(const workload_state& state)
{
    std::uint64_t hash = 0;
    for (const auto& item : state.items)
    {
        hash = hash * multiplier + transform(item.input, {state.config.rounds});
    }

    return hash;
}

/// @brief Compose map/fold/ordered-merge phases through the generic direct dispatch API.
workload_run submit_workload(const job_dispatcher& jobs,
                             const std::shared_ptr<workload_state>& state)
{
    auto map = jobs.run(job_label{"map", "map"}, state, map_stage);
    auto fold = jobs.after(map, job_label{"fold", "fold"}, state, fold_stage);
    auto merge = jobs.after(fold, job_label{"merge", "merge"}, state, merge_stage);
    return {std::move(map), std::move(fold), std::move(merge)};
}
} // namespace atlas::jobs::workloads
