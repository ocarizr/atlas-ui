/** @file @brief Exact root reuse and two joined phases commit only a complete result/cache. */
#include "layout_state.hpp"
#include <algorithm>

namespace atlas::ui
{
/// @brief Failed preparation/measure/arrange/flattening preserves the previously committed cache.
std::shared_ptr<const detail::layout_snapshot_data>
layout_engine::impl::compute(const element_snapshot& snapshot, const jobs::job_options& options)
{
    stats = {};
    (void)dispatcher.with_options(options);
    if (options.cancellation.stop_requested())
    {
        throw jobs::job_cancelled{};
    }

    if (snapshot.session() == 0 || snapshot.revision() == 0)
    {
        throw std::invalid_argument("UI layout requires a tree-owned snapshot");
    }

    if (snapshot.elements().size() > limits.max_nodes)
    {
        throw std::overflow_error("UI layout node budget exhausted");
    }

    if (last && last->session == snapshot.session() && last->revision == snapshot.revision() &&
        last->viewport == snapshot.viewport())
    {
        stats.cached_roots = last->root_count;
        return last;
    }

    auto forest = detail::prepare_layout(snapshot, memory);
    std::pmr::vector<detail::arrange_output> next(forest.records.size(), {}, memory.get());
    std::pmr::vector<detail::measure_input> pending(0, memory.get());
    pending.reserve(forest.roots.size());
    const bool reusable =
        last && last->session == snapshot.session() && last->viewport == snapshot.viewport();
    std::size_t dirty_nodes = 0;
    for (const auto& root : forest.roots)
    {
        const auto index = root->nodes[0].target.index;
        if (reusable && index < cache.size() && cache[index] &&
            cache[index]->measured->source->nodes == root->nodes &&
            cache[index]->measured->source->slots == root->slots &&
            cache[index]->measured->source->stack_slots == root->stack_slots)
        {
            next[index] = cache[index];
            ++stats.cached_roots;
        }
        else
        {
            dirty_nodes += root->nodes.size();
            pending.push_back(root);
        }
    }

    const bool parallel =
        pending.size() > 1 && dirty_nodes >= limits.min_parallel_nodes && limits.max_chunks > 1;
    const auto arranged = evaluate(pending, snapshot.viewport(), options, parallel);

    stats.measured_nodes = stats.arranged_nodes = dirty_nodes;
    for (const auto& root : arranged)
    {
        next[root->items[0].target.index] = root;
    }

    auto output = std::make_shared<detail::layout_snapshot_data>(memory);
    output->session = snapshot.session();
    output->revision = snapshot.revision();
    output->viewport = snapshot.viewport();
    output->root_count = forest.roots.size();
    output->items.resize(snapshot.elements().size());
    for (const auto& root : forest.roots)
    {
        const auto& branch = next[root->nodes[0].target.index];
        for (const auto& item : branch->items)
        {
            output->items[forest.records[item.target.index]] = item;
        }
    }

    if (options.cancellation.stop_requested())
    {
        throw jobs::job_cancelled{};
    }

    cache.swap(next);
    last = output;
    return output;
}
} // namespace atlas::ui
