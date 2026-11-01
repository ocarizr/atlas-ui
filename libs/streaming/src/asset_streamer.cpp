/** @file
 * @brief Logical read admission, persistent I/O ownership and draining lifecycle.
 */
#include "streamer_state.hpp"
#include <atlas/streaming/asset_streamer.hpp>
#include <stdexcept>
#include <thread>

namespace atlas::streaming
{
/// @brief Lifecycle owner; native handles and CPU jobs retain only shared execution state.
struct asset_streamer::impl
{
    /// @brief Validate state before creating the one persistent blocking-storage thread.
    impl(jobs::job_system& jobs, asset_catalog catalog, streamer_limits limits,
         std::shared_ptr<std::pmr::memory_resource> buffers)
        : state{std::make_shared<detail::streamer_state>(jobs, std::move(catalog), limits,
                                                         std::move(buffers))},
          io{[shared = state]
             {
                 shared->run_io();
             }}
    {
    }

    /// @brief Shared execution state retained by accepted work.
    std::shared_ptr<detail::streamer_state> state;
    /// @brief Coordinator responsible for stopping external producers and draining.
    const std::thread::id coordinator{std::this_thread::get_id()};
    /// @brief Persistent disk-I/O owner; CPU workers never perform blocking file acquisition.
    std::jthread io;
};

/// @brief Construct with an application-owned pool and an optional reusable payload allocator.
asset_streamer::asset_streamer(jobs::job_system& jobs, asset_catalog catalog,
                               streamer_limits limits,
                               std::shared_ptr<std::pmr::memory_resource> buffers)
    : impl_{std::make_unique<impl>(jobs, std::move(catalog), limits, std::move(buffers))}
{
}

/// @brief Drain on the creating coordinator; misuse cannot safely destroy live I/O state.
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

/// @brief Reserve request and payload budgets before making work visible to storage or CPU workers.
std::future<void> asset_streamer::read(read_request request, read_completion completion,
                                       read_options options)
{
    if (!completion)
    {
        throw std::invalid_argument{"An asset read needs a completion callback"};
    }

    if (request.size > impl_->state->limits.max_read_bytes)
    {
        throw std::length_error{"Asset read exceeds the configured byte limit"};
    }

    auto operation = std::make_shared<detail::read_operation>();
    operation->request = std::move(request);
    operation->completion = std::move(completion);
    operation->options = std::move(options);
    auto finished = operation->finished.get_future();
    auto& state = *impl_->state;
    bool file = false;
    std::exception_ptr storage_failure;
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

        operation->reservation = detail::reserve_buffer(state.memory, operation->request.size,
                                                        state.limits.max_resident_bytes);
        const auto source = state.catalog.find(operation->request.asset_id);
        file = source != state.catalog.end() &&
               std::holds_alternative<loose_file_asset>(source->second);
        if (file)
        {
            storage_failure = state.storage_failure;
            if (!storage_failure)
            {
                operation->cancellation_wakeup.emplace(operation->options.cancellation,
                                                       detail::io_wakeup{impl_->state});
                state.queue.push_back(operation);
            }
        }

        ++state.pending;
    }
    if (storage_failure)
    {
        state.schedule(operation, storage_failure);
    }
    else if (file)
    {
        state.files->wake();
    }
    else
    {
        state.schedule(operation);
    }

    return finished;
}

/// @brief Read admission/terminal counters under their lock and byte/copy counters atomically.
streamer_statistics asset_streamer::statistics() const
{
    auto& state = *impl_->state;
    const std::lock_guard lock{state.mutex};
    const auto io = state.files->statistics();
    return {state.pending,
            state.memory->resident.load(),
            state.memory->peak.load(),
            state.serial_copies.load(),
            state.parallel_copies.load(),
            state.completed,
            state.failed,
            state.io_in_flight.load(),
            state.peak_io_in_flight.load(),
            io.cancellation_requests,
            io.cancellation_races,
            io.cancellation_errors};
}

/// @brief Close admission, join storage and wait only on the coordinator for accepted deliveries.
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
    state.files->wake();
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
