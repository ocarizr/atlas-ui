/**
 * @file
 * @brief Capture-free recording adapter over an existing application job dispatcher.
 */
#pragma once
#include <atlas/jobs/job_dispatcher.hpp>
#include <atlas/renderer/recording.hpp>

namespace atlas::renderer_jobs
{
/// @brief Borrow an existing pool; never create workers or submit graphics queues here.
class executor final : public renderer::recording_executor
{
  public:
    explicit executor(
        jobs::job_dispatcher dispatcher); ///< Retain a non-owning dispatcher view and its options.
    void execute(std::span<renderer::recording_task* const> tasks)
        override; ///< Join every accepted child even on recording failure/cancellation.
  private:
    jobs::job_dispatcher dispatcher_; ///< Application owns and outlives the worker pool.
};
} // namespace atlas::renderer_jobs
