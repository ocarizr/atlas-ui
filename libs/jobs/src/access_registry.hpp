#pragma once

#include <atlas/jobs/job_sanitizer.hpp>

#include <cstdint>
#include <mutex>

namespace atlas::jobs::detail
{
struct task_state;

struct access_region
{
    std::uintptr_t begin;
    std::uintptr_t end;
    access_mode mode;
};

struct active_access
{
    std::size_t token;
    const task_state* owner;
    std::string name;
    std::vector<access_region> regions;
};

class access_registry
{
  public:
    explicit access_registry(job_sanitizer_options options);
    std::size_t acquire(const task_state& owner, std::span<const memory_access> requests);
    void release(std::size_t token) noexcept;
    bool finish(const task_state& owner);
    job_sanitizer_report report() const;

  private:
    void check(const active_access& incoming);
    void record(diagnostic_kind kind, const std::string& name, const std::string& other = {});
    job_sanitizer_options options_;
    mutable std::mutex mutex_;
    std::vector<active_access> active_;
    std::vector<job_diagnostic> diagnostics_;
    std::size_t next_token_{1};
    std::size_t dropped_{0};
};
} // namespace atlas::jobs::detail
