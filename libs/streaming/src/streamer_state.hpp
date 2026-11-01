/** @file
 * @brief Shared admission, storage and ready-job ownership for one streamer.
 */
#pragma once
#include "buffer_memory.hpp"
#include "file_io.hpp"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>

namespace atlas::streaming::detail
{
/// @brief Shared producer/consumer state used weakly by stop callbacks.
struct streamer_state;

/// @brief Allocation-free wake capability that cannot keep a queued request/state cycle alive.
struct io_wakeup
{
    /// @brief Non-owning state reference; a successful lock pins the backend through wake.
    std::weak_ptr<streamer_state> state;
    /// @brief Wake storage processing after a stop request, without touching its native containers.
    void operator()() const noexcept;
};

/// @brief One accepted read retained until CPU delivery and admission retirement.
struct read_operation
{
    /// @brief Owned logical asset range.
    read_request request;
    /// @brief Consumer callback, invoked only by CPU jobs.
    read_completion completion;
    /// @brief Explicit priority and cooperative cancellation signal.
    read_options options;
    /// @brief Output-byte reservation, shared with any retained result.
    std::shared_ptr<buffer_reservation> reservation;
    /// @brief Registered for queued and active file requests; destruction joins an executing stop
    /// callback.
    std::optional<std::stop_callback<io_wakeup>> cancellation_wakeup;
    /// @brief Coordinator observation of callback completion or dispatch/callback failure.
    std::promise<void> finished;
};

/// @brief Shared catalog, allocator and worker-pool client retained by accepted work.
struct streamer_state : std::enable_shared_from_this<streamer_state>
{
    /// @brief Validate limits before starting persistent storage resources.
    streamer_state(jobs::job_system& scheduler, asset_catalog assets, streamer_limits budgets,
                   std::shared_ptr<std::pmr::memory_resource> resource,
                   std::unique_ptr<file_io> storage = {});
    /// @brief Retire admission and publish the future atomically with respect to draining.
    void retire(const std::shared_ptr<read_operation>& operation, const std::exception_ptr& failure,
                bool read_failed);
    /// @brief Deliver prepared bytes/errors, containing callback exceptions and retiring once.
    void deliver(const std::shared_ptr<read_operation>& operation, read_result result);
    /// @brief Dispatch capture-free ready work using the shared application's current job API.
    void schedule(const std::shared_ptr<read_operation>& operation,
                  std::optional<read_result> prepared = std::nullopt);
    /// @brief Acquire file bytes off the CPU pool and enqueue only ready completion work.
    void run_io();
    /// @brief Copy memory serially or through joined disjoint children without waiting on workers.
    void read_memory(jobs::job_context& context, const std::shared_ptr<read_operation>& operation);
    /// @brief Borrowed pool used only for runtime load observations; must outlive draining.
    jobs::job_system& jobs;
    /// @brief Injected non-owning dispatch capability; no consumer-owned CPU pool is created.
    jobs::job_dispatcher dispatcher;
    /// @brief Immutable logical source registrations.
    const asset_catalog catalog;
    /// @brief Immutable validated budgets and grain policy.
    const streamer_limits limits;
    /// @brief Shared byte accounting and payload resource.
    std::shared_ptr<buffer_memory> memory;
    /// @brief Selected backend owned through all pending storage and stop-callback activity.
    std::unique_ptr<file_io> files;
    /// @brief Protects admission, queue and terminal counters.
    mutable std::mutex mutex;
    /// @brief Wakes the coordinator when all accepted deliveries retire.
    std::condition_variable idle;
    /// @brief FIFO disk requests; CPU priority is applied after acquisition.
    std::deque<std::shared_ptr<read_operation>> queue;
    /// @brief Terminal storage error; later file requests are delivered as errors without
    /// enqueueing.
    std::exception_ptr storage_failure;
    /// @brief Current storage occupancy, including submission and pending OS requests.
    std::atomic<std::size_t> io_in_flight{0};
    /// @brief High-water storage occupancy, updated only by the I/O owner.
    std::atomic<std::size_t> peak_io_in_flight{0};
    /// @brief Accepted operations not yet retired.
    std::size_t pending{0};
    /// @brief Retired operation count, guarded by mutex.
    std::size_t completed{0};
    /// @brief Retired acquisition/cancellation/callback failure count, guarded by mutex.
    std::size_t failed{0};
    /// @brief Serial memory-copy path observations.
    std::atomic<std::size_t> serial_copies{0};
    /// @brief Parallel memory-copy path observations.
    std::atomic<std::size_t> parallel_copies{0};
    /// @brief Whether admission is open.
    bool accepting{true};
};
} // namespace atlas::streaming::detail
