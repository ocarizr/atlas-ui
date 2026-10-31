#pragma once

#include <atlas/jobs/job_graph.hpp>
#include <atlas/jobs/job_memory.hpp>
#include <atlas/jobs/job_policy.hpp>

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace atlas::jobs
{
struct job_label
{
    job_label(std::string value, std::string group = {})
        : name{std::move(value)}, category{std::move(group)}
    {
    }
    job_label(const char* value) : job_label{value ? std::string{value} : std::string{}} {}
    std::string name;
    std::string category;
};

struct task_trace
{
    std::uint64_t id{0};
    std::uint64_t parent_id{0};
    std::uint64_t dispatch_index{0};
    job_priority priority{job_priority::normal};
    std::string name;
    std::string category;
    std::vector<std::uint64_t> prerequisites;
    std::size_t worker_index{0};
    job_status status{job_status::pending};
    std::chrono::nanoseconds submitted{};
    std::chrono::nanoseconds started{};
    std::chrono::nanoseconds body_finished{};
    std::chrono::nanoseconds completed{};
    scratch_statistics scratch;
};

struct job_trace_report
{
    bool enabled;
    std::size_t worker_count;
    std::size_t dropped;
    std::size_t pending;
    std::vector<task_trace> tasks;
};
} // namespace atlas::jobs
