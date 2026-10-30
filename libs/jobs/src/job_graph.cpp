#include <atlas/jobs/job_graph.hpp>

#include "graph_state.hpp"

#include <algorithm>
#include <iterator>
#include <utility>

namespace atlas::jobs
{
struct job_graph::impl
{
    struct node
    {
        std::string name;
        std::function<void()> callback;
        std::vector<std::size_t> successors;
    };
    std::shared_ptr<const void> identity{std::make_shared<const int>(0)};
    std::vector<node> nodes;
};

namespace
{
void validate_acyclic(const std::vector<detail::graph_node>& nodes)
{
    std::vector<std::size_t> remaining;
    std::vector<std::size_t> ready;
    remaining.reserve(nodes.size());
    ready.reserve(nodes.size());
    for (std::size_t index = 0; index < nodes.size(); ++index)
    {
        remaining.push_back(nodes[index].remaining);
        if (remaining.back() == 0)
        {
            ready.push_back(index);
        }
    }
    for (std::size_t head = 0; head < ready.size(); ++head)
    {
        for (const auto successor : nodes[ready[head]].successors)
        {
            if (--remaining[successor] == 0)
            {
                ready.push_back(successor);
            }
        }
    }
    if (ready.size() != nodes.size())
    {
        const auto blocked = std::find_if(remaining.begin(), remaining.end(),
                                          [](std::size_t count) { return count != 0; });
        const auto index = static_cast<std::size_t>(blocked - remaining.begin());
        throw std::invalid_argument{"Dependency cycle blocks job '" + nodes[index].name + "'"};
    }
}
} // namespace

job_graph::job_graph() : impl_{std::make_unique<impl>()} {}
job_graph::~job_graph() = default;
job_graph::job_graph(job_graph&&) noexcept = default;
job_graph& job_graph::operator=(job_graph&&) noexcept = default;

job_id job_graph::add(std::string name, std::function<void()> callback)
{
    if (!impl_)
    {
        throw std::logic_error{"Cannot use a moved-from job graph"};
    }
    if (name.empty() || !callback)
    {
        throw std::invalid_argument{"A graph job needs a nonempty name and callback"};
    }
    const auto index = impl_->nodes.size();
    impl_->nodes.push_back({std::move(name), std::move(callback), {}});
    job_id result;
    result.owner_ = impl_->identity;
    result.index_ = index;
    return result;
}

void job_graph::precedes(const job_id& prerequisite, const job_id& dependent)
{
    if (!impl_)
    {
        throw std::logic_error{"Cannot use a moved-from job graph"};
    }
    if (prerequisite.owner_ != impl_->identity || dependent.owner_ != impl_->identity ||
        prerequisite.index_ >= impl_->nodes.size() || dependent.index_ >= impl_->nodes.size())
    {
        throw std::invalid_argument{"Dependency IDs must belong to this job graph"};
    }
    auto& successors = impl_->nodes[prerequisite.index_].successors;
    if (std::find(successors.begin(), successors.end(), dependent.index_) == successors.end())
    {
        successors.push_back(dependent.index_);
    }
}

std::shared_ptr<detail::graph_state> job_graph::snapshot() const
{
    if (!impl_)
    {
        throw std::logic_error{"Cannot submit a moved-from job graph"};
    }
    auto state = std::make_shared<detail::graph_state>();
    state->identity = impl_->identity;
    state->nodes.resize(impl_->nodes.size());
    state->propagation.resize(impl_->nodes.size());
    for (std::size_t index = 0; index < impl_->nodes.size(); ++index)
    {
        const auto& source = impl_->nodes[index];
        auto& node = state->nodes[index];
        node.name = source.name;
        node.successors = source.successors;
        for (const auto successor : source.successors)
        {
            ++state->nodes[successor].remaining;
        }
        state->pending.emplace_back();
        node.entry = std::prev(state->pending.end());
        node.entry->callback = source.callback;
        node.entry->index = index;
    }
    validate_acyclic(state->nodes);
    return state;
}

graph_error::graph_error(std::string job_name, const std::exception_ptr& cause)
    : std::runtime_error{"Job '" + job_name + "' failed"}, job_name_{std::move(job_name)},
      cause_{cause}
{
}

const std::string& graph_error::job_name() const noexcept
{
    return job_name_;
}

std::exception_ptr graph_error::cause() const noexcept
{
    return cause_;
}

graph_handle::graph_handle(std::shared_ptr<detail::graph_state> state) : state_{std::move(state)} {}

void graph_handle::validate(const job_id& job) const
{
    if (!state_ || job.owner_ != state_->identity || job.index_ >= state_->nodes.size())
    {
        throw std::invalid_argument{"Job ID does not belong to this graph execution"};
    }
}

job_status graph_handle::status(const job_id& job) const
{
    validate(job);
    const std::lock_guard lock{state_->mutex};
    return state_->nodes[job.index_].status;
}

std::exception_ptr graph_handle::exception(const job_id& job) const
{
    validate(job);
    const std::lock_guard lock{state_->mutex};
    return state_->nodes[job.index_].error;
}
} // namespace atlas::jobs
