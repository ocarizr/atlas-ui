/**
 * @file
 * @brief Joined executor contract for exclusive recording without owning worker threads.
 */
#pragma once
#include <atomic>
#include <cstdint>
#include <exception>
#include <span>

namespace atlas::renderer
{
/// @brief Coarse scheduling policy selected from measured CPU recording cost.
struct recording_options
{
    std::uint32_t min_draws_per_batch = 4096; ///< Minimum average draws before adding a batch.
    std::uint32_t max_batches = 4;            ///< Also limited by the renderer's capacity.
    bool allow_software_parallel =
        false;             ///< Opt in after measuring software rasterizer CPU contention.
    void validate() const; ///< Reject zero grain or unsupported concurrency limits.
};

/// @brief Observational result for the last successfully submitted frame.
struct recording_statistics
{
    std::uint32_t command_lists = 0;  ///< Lists submitted in stable frame order.
    std::uint32_t worker_batches = 0; ///< Batches handed to the borrowed executor.
    std::uint64_t draws = 0;          ///< Total validated draw packets.
};

/// @brief One exclusive operation, borrowed until the executor joins all accepted work.
class recording_task
{
  public:
    virtual ~recording_task() = default; ///< Release only after every accepted task has joined.
    void run();          ///< Run exactly once and publish failures for coordinator verification.
    void verify() const; ///< Rethrow failure or reject an omitted task.
  protected:
    virtual void record() = 0; ///< Record into this task's exclusive native context.
  private:
    std::atomic<unsigned> state_{0}; ///< Pending, recording, succeeded or failed publication.
    std::exception_ptr failure_;     ///< Published before the failed terminal state.
};

/// @brief Synchronous join boundary over an application's existing worker scheduler.
class recording_executor
{
  public:
    virtual ~recording_executor() = default; ///< Owns no renderer or device.
    virtual void execute(
        std::span<recording_task* const> tasks) = 0; ///< On success run each task once; always join
                                                     ///< accepted work before return or throw.
};
} // namespace atlas::renderer
