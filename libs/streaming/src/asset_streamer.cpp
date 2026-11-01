/**
 * @file
 * @brief Exclusive storage acquisition with persistent I/O and job-based ready delivery.
 */

#include <atlas/streaming/asset_streamer.hpp>

#include "asset_reader.hpp"

#include <condition_variable>
#include <deque>
#include <ios>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

namespace atlas::streaming
{
namespace
{
/// @brief Accepted request, callback and promise retained until its completion delivery retires.
struct read_operation
{
    /// @brief Owned asset-relative request retained until ready completion.
    read_request request;
    /// @brief Retained callback/promise making the accepted operation observable to its
    /// coordinator.
    read_completion completion;
    /// @brief Aggregate completion signal or terminal flag used by the enclosing operation.
    std::promise<void> finished;
};

/// @brief Shared catalog/admission/I/O state kept alive through accepted completion jobs.
struct streamer_state : std::enable_shared_from_this<streamer_state>
{
    /// @brief Validate request/byte admission limits before accepting any reads.
    streamer_state(jobs::job_system& scheduler, asset_catalog assets, streamer_limits budgets)
        : jobs{scheduler}, catalog{std::move(assets)}, limits{budgets}
    {
        if (limits.max_read_bytes == 0 || limits.max_pending_requests == 0 ||
            limits.max_read_bytes >
                static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()))
        {
            throw std::invalid_argument{"Streamer limits must be positive and representable"};
        }
    }

    /// @brief Release one accepted streaming operation and notify draining shutdown.
    void retire()
    {
        const std::lock_guard lock{mutex};
        --pending;
        idle.notify_all();
    }

    /// @brief Dispatch a prepared read result as CPU work and retire admission if dispatch fails.
    void schedule(const std::shared_ptr<read_operation>& operation,
                  std::optional<read_result> prepared = std::nullopt)
    {
        try
        {
            jobs.submit(
                [self = shared_from_this(), operation, prepared = std::move(prepared)]() mutable
                {
                    try
                    {
                        auto result = prepared
                                          ? std::move(*prepared)
                                          : detail::read_asset(self->catalog, operation->request);
                        operation->completion(std::move(result));
                        operation->finished.set_value();
                    }
                    catch (...)
                    {
                        operation->finished.set_exception(std::current_exception());
                    }

                    self->retire();
                });
        }
        catch (...)
        {
            // A stopped scheduler cannot dispatch a callback on the correct thread.
            operation->finished.set_exception(std::current_exception());
            retire();
        }
    }

    /// @brief Drain queued file reads on the persistent I/O thread, then schedule ready
    /// completions.
    void run_io()
    {
        while (true)
        {
            std::shared_ptr<read_operation> operation;
            {
                std::unique_lock lock{mutex};
                available.wait(lock,
                               [this]
                               {
                                   return !queue.empty() || !accepting;
                               });
                if (queue.empty())
                {
                    return;
                }

                operation = std::move(queue.front());
                queue.pop_front();
            }
            schedule(operation, detail::read_asset(catalog, operation->request));
        }
    }

    /// @brief Borrowed application-owned pool, which must outlive streamer draining.
    jobs::job_system& jobs;
    /// @brief Immutable logical asset-source registration map.
    const asset_catalog catalog;
    /// @brief Validated per-read and in-flight streaming admission budgets.
    const streamer_limits limits;
    /// @brief Synchronization for the enclosing shared registry/capture/execution state.
    std::mutex mutex;
    /// @brief Condition variable waking persistent workers when work or shutdown becomes available.
    std::condition_variable available;
    /// @brief Condition variable used to observe accepted streaming operations retiring.
    std::condition_variable idle;
    /// @brief Pending accepted I/O operations or eligible work in the enclosing subsystem.
    std::deque<std::shared_ptr<read_operation>> queue;
    /// @brief Accepted read operations not yet retired, including completion callbacks in flight.
    std::size_t pending{0};
    /// @brief Whether new root submissions/requests/messages are still admitted.
    bool accepting{true};
};
} // namespace

/// @brief Private implementation state; callers interact through the enclosing public API.
struct asset_streamer::impl
{
    /// @brief Create shared storage state and start one persistent I/O worker owned by the creating
    /// coordinator.
    impl(jobs::job_system& jobs, asset_catalog catalog, streamer_limits limits)
        : state{std::make_shared<streamer_state>(jobs, std::move(catalog), limits)},
          io{[shared = state]
             {
                 shared->run_io();
             }}
    {
    }

    /// @brief Shared execution/workload state retained through all accepted callbacks.
    std::shared_ptr<streamer_state> state;
    /// @brief Creating coordinator thread that alone may shut down/destroy this streamer.
    const std::thread::id coordinator{std::this_thread::get_id()};
    /// @brief Persistent dedicated I/O thread; disk reads do not block the CPU worker pool.
    std::jthread io;
};

/// @brief Own an immutable catalog and persistent I/O thread while borrowing a pool that must
/// outlive draining.
asset_streamer::asset_streamer(jobs::job_system& jobs, asset_catalog catalog,
                               streamer_limits limits)
    : impl_{std::make_unique<impl>(jobs, std::move(catalog), limits)}
{
}

/// @brief Drain accepted work and release persistent resources; callers must obey the enclosing
/// coordinator/lifetime contract.
asset_streamer::~asset_streamer()
{
    try
    {
        shutdown();
    }
    catch (...)
    {
        std::terminate();
    }
}

/// @brief Request an exact asset range; accepted results are delivered as jobs, never by blocking a
/// CPU worker.
std::future<void> asset_streamer::read(read_request request, read_completion completion)
{
    if (!completion)
    {
        throw std::invalid_argument{"An asset read needs a completion callback"};
    }

    if (request.size > impl_->state->limits.max_read_bytes)
    {
        throw std::length_error{"Asset read exceeds the configured byte limit"};
    }

    auto operation = std::make_shared<read_operation>();
    operation->request = std::move(request);
    operation->completion = std::move(completion);
    auto finished = operation->finished.get_future();
    auto& state = *impl_->state;
    bool file = false;
    {
        const std::lock_guard lock{state.mutex};
        if (!state.accepting)
        {
            throw std::logic_error{"The asset streamer is shutting down"};
        }

        if (state.pending == state.limits.max_pending_requests)
        {
            throw std::overflow_error{"The asset streamer request budget is exhausted"};
        }

        const auto source = state.catalog.find(operation->request.asset_id);
        file = source != state.catalog.end() &&
               std::holds_alternative<loose_file_asset>(source->second);
        if (file)
        {
            state.queue.push_back(operation);
        }

        ++state.pending;
    }
    if (file)
    {
        state.available.notify_one();
    }
    else
    {
        state.schedule(operation);
    }

    return finished;
}

/// @brief On the creating coordinator, reject new reads, drain accepted callbacks and join I/O
/// before destroying storage.
void asset_streamer::shutdown()
{
    if (std::this_thread::get_id() != impl_->coordinator)
    {
        throw std::logic_error{"Only the creating coordinator may shut down the streamer"};
    }

    auto& state = *impl_->state;
    {
        const std::lock_guard lock{state.mutex};
        state.accepting = false;
    }
    state.available.notify_all();
    if (impl_->io.joinable())
    {
        impl_->io.join();
    }

    std::unique_lock lock{state.mutex};
    state.idle.wait(lock,
                    [&state]
                    {
                        return state.pending == 0;
                    });
}
} // namespace atlas::streaming
