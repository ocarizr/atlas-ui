/**
 * @file
 * @brief Sealed-graph eligibility publication and stable failure propagation.
 */

#include "graph_state.hpp"

#include <utility>

namespace atlas::jobs::detail
{
/// @brief Splice a prepared eligible node into the ready queue while retaining its graph execution.
void graph_state::make_ready(const std::shared_ptr<graph_state>& self, std::size_t index,
                             ready_queue& ready)
{
    auto& node = nodes[index];
    node.status = job_status::ready;
    node.entry->graph = self;
    ready.splice(ready.end(), pending, node.entry);
}

/// @brief Publish prepared zero-prerequisite graph nodes into the ready queue.
std::size_t graph_state::publish(const std::shared_ptr<graph_state>& self, ready_queue& ready)
{
    std::size_t roots = 0;
    for (std::size_t index = 0; index < nodes.size(); ++index)
    {
        if (nodes[index].remaining == 0)
        {
            make_ready(self, index, ready);
            ++roots;
        }
    }

    return roots;
}

/// @brief Commit one node result, propagate blocked successors and complete the aggregate graph
/// when terminal.
graph_completion graph_state::finish(const std::shared_ptr<graph_state>& self, std::size_t index,
                                     const std::exception_ptr& error, queue_transition queues)
{
    const std::lock_guard lock{mutex};
    nodes[index].error = error;
    nodes[index].status = nodes[index].error ? job_status::failed : job_status::succeeded;
    ++terminal;
    graph_completion result;
    std::size_t tail = 1;
    propagation[0] = index;
    for (std::size_t head = 0; head < tail; ++head)
    {
        const auto& predecessor = nodes[propagation[head]];
        for (const auto successor : predecessor.successors)
        {
            auto& node = nodes[successor];
            node.blocked = node.blocked || predecessor.status != job_status::succeeded;
            if (--node.remaining != 0)
            {
                continue;
            }

            ++result.released;
            if (node.blocked)
            {
                node.status = job_status::skipped;
                // Captured objects may run user cleanup. Release them after both locks.
                queues.retired.splice(queues.retired.end(), pending, node.entry);
                ++terminal;
                ++result.skipped;
                propagation[tail++] = successor;
            }
            else
            {
                make_ready(self, successor, queues.ready);
            }
        }
    }

    result.finished = terminal == nodes.size();
    return result;
}
} // namespace atlas::jobs::detail
