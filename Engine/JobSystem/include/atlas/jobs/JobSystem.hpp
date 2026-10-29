#pragma once

#include <cstddef>
#include <functional>
#include <future>
#include <memory>

namespace atlas::jobs
{
class JobSystem;

class JobHandle
{
  public:
    JobHandle() = default;

  private:
    friend class JobSystem;
    JobHandle(std::shared_future<void> completion, std::weak_ptr<const void> owner);

    std::shared_future<void> completion_;
    std::weak_ptr<const void> owner_;
};

struct JobStatistics
{
    std::size_t workers;
    std::size_t queued;
    std::size_t running;
    std::size_t submitted;
    std::size_t completed;
};

// Independent jobs only in this foundation. Dependency graphs are a later stage.
// Jobs must not call wait, wait_idle, shutdown, or destroy a scheduler.
class JobSystem
{
  public:
    explicit JobSystem(std::size_t worker_count);
    ~JobSystem();
    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;
    JobSystem(JobSystem&&) = delete;
    JobSystem& operator=(JobSystem&&) = delete;

    JobHandle submit(std::function<void()> job);
    void wait(const JobHandle& job) const;
    void wait_idle() const;
    void shutdown();
    [[nodiscard]] JobStatistics statistics() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace atlas::jobs
