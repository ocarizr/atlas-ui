/** @file
 * @brief Ready-job dispatch, exact retirement and persistent disk acquisition.
 */
#include "streamer_state.hpp"
#include <ios>
#include <limits>
#include <stdexcept>

namespace atlas::streaming::detail
{
/// @brief Validate budgets and retain a client of the application-owned pool.
streamer_state::streamer_state(jobs::job_system& scheduler, asset_catalog assets,
                               streamer_limits budgets,
                               std::shared_ptr<std::pmr::memory_resource> resource,
                               std::unique_ptr<file_io> storage)
    : jobs{scheduler}, dispatcher{scheduler.dispatcher()}, catalog{std::move(assets)},
      limits{budgets}, memory{std::make_shared<buffer_memory>(std::move(resource))},
      files{std::move(storage)}
{
    if (limits.max_read_bytes == 0 || limits.max_pending_requests == 0 ||
        limits.max_resident_bytes == 0 || limits.max_io_requests == 0 ||
        limits.copy_grain_bytes == 0 || limits.copy_grain_bytes % 64 != 0 ||
        limits.max_read_bytes >
            static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()))
    {
        throw std::invalid_argument{
            "Streamer limits must be positive, representable and use cache-line grains"};
    }

    if (!files)
    {
        files = make_file_io();
    }
}

/// @brief Release bytes before making the future ready; shutdown observes ready futures and retired
/// admission.
void streamer_state::retire(const std::shared_ptr<read_operation>& operation,
                            const std::exception_ptr& failure, bool read_failed)
{
    operation->reservation.reset();
    const std::lock_guard lock{mutex};
    --pending;
    ++completed;
    failed += static_cast<std::size_t>(read_failed || static_cast<bool>(failure));
    if (failure)
    {
        operation->finished.set_exception(failure);
    }
    else
    {
        operation->finished.set_value();
    }

    idle.notify_all();
}

/// @brief Invoke a consumer once on a CPU worker, then release result ownership before retirement.
void streamer_state::deliver(const std::shared_ptr<read_operation>& operation, read_result result)
{
    if (operation->options.cancellation.stop_requested())
    {
        result = std::make_exception_ptr(jobs::job_cancelled{});
    }

    const bool read_failed = std::holds_alternative<std::exception_ptr>(result);
    std::exception_ptr failure;
    try
    {
        operation->completion(std::move(result));
    }
    catch (...)
    {
        failure = std::current_exception();
    }

    result = std::exception_ptr{};
    retire(operation, failure, read_failed);
}

/// @brief Use explicit retained input and a capture-free callback; cancellation never suppresses
/// cleanup.
void streamer_state::schedule(const std::shared_ptr<read_operation>& operation,
                              std::optional<read_result> prepared)
{
    /// @brief Owned input retained until ready delivery or the memory-copy parent joins.
    struct ready_work
    {
        /// @brief Shared streamer execution state.
        std::shared_ptr<streamer_state> state;
        /// @brief Accepted operation and its completion observer.
        std::shared_ptr<read_operation> operation;
        /// @brief Bytes/error acquired off the CPU pool, or no value for memory work.
        std::optional<read_result> prepared;
    };

    try
    {
        dispatcher.with_options({operation->options.priority})
            .run(jobs::job_label{"stream.read", "streaming"},
                 ready_work{shared_from_this(), operation, std::move(prepared)},
                 [](jobs::job_context& context, ready_work& work)
                 {
                     if (work.prepared)
                     {
                         work.state->deliver(work.operation, std::move(*work.prepared));
                     }
                     else
                     {
                         work.state->read_memory(context, work.operation);
                     }
                 });
    }
    catch (...)
    {
        prepared.reset();
        retire(operation, std::current_exception(), true);
    }
}

} // namespace atlas::streaming::detail
