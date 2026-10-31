#pragma once

#include <atlas/jobs/job_system.hpp>

#include <atomic>
#include <limits>
#include <memory>
#include <vector>

namespace atlas::jobs::workloads
{
struct workload_config
{
    std::size_t size{4096};
    std::size_t grain{128};
    std::size_t rounds{64};
    std::uint64_t seed{42};
    std::size_t fail_index{std::numeric_limits<std::size_t>::max()};
    std::size_t dirty_stride{1}; // 0 keeps the cached snapshot; >1 changes a subset.
};

struct workload_state;
struct work_item
{
    std::uint64_t input;
    std::uint64_t output{0};
    workload_state* owner;
    std::size_t index;
    bool dirty{true};
};
struct partial_result
{
    std::uint64_t hash{0};
    std::size_t length{0};
};
struct workload_state
{
    workload_config config;
    std::vector<work_item> items;
    std::vector<partial_result> partials;
    std::atomic<std::size_t> mapped{0};
    std::atomic<std::size_t> folded{0};
    std::atomic<std::size_t> computed{0};
    std::atomic<int> phase{0};
    std::uint64_t checksum{0};
};
struct workload_run
{
    task_handle map;
    task_handle fold;
    task_handle merge;
};

std::shared_ptr<workload_state> prepare_workload(workload_config config);
std::uint64_t sequential_reference(const workload_state& state);
workload_run submit_workload(const job_dispatcher& jobs,
                             const std::shared_ptr<workload_state>& state);
} // namespace atlas::jobs::workloads
