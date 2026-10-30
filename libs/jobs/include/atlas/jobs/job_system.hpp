#pragma once

#include <cstddef>
#include <functional>
#include <future>
#include <memory>

namespace atlas::jobs
{
class job_system;

class job_handle
{
  public:
    job_handle() = default;

  private:
    friend class job_system;
    job_handle(std::shared_future<void> completion, std::weak_ptr<const void> owner);

    std::shared_future<void> completion_;
    std::weak_ptr<const void> owner_;
};

struct job_statistics
{
    std::size_t workers;
    std::size_t queued;
    std::size_t running;
    std::size_t submitted;
    std::size_t completed;
};

// Independent jobs only in this foundation. Dependency graphs are a later stage.
// Jobs must not call wait, wait_idle, shutdown, or destroy a scheduler.
class job_system
{
  public:
    explicit job_system(std::size_t worker_count);
    ~job_system();
    job_system(const job_system&) = delete;
    job_system& operator=(const job_system&) = delete;
    job_system(job_system&&) = delete;
    job_system& operator=(job_system&&) = delete;

    job_handle submit(std::function<void()> job);
    void wait(const job_handle& job) const;
    void wait_idle() const;
    void shutdown();
    [[nodiscard]] job_statistics statistics() const;

  private:
    struct impl;
    std::unique_ptr<impl> impl_;
};
} // namespace atlas::jobs
