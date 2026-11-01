/** @file
 * @brief Bounded storage submission, queued cancellation and completion-driven ready jobs.
 */
#include "streamer_state.hpp"
#include <algorithm>

namespace atlas::streaming::detail
{
/// @brief Pin the backend while notifying it; expired request/state ownership is harmless.
void io_wakeup::operator()() const noexcept
{
    if (const auto owner = state.lock())
    {
        owner->files->wake();
    }
}

namespace
{
/// @brief Remove one cancelled or backend-failed queued request without entering storage.
std::shared_ptr<read_operation> take_rejected(streamer_state& state)
{
    const std::lock_guard lock{state.mutex};
    if (const auto error = state.files->failure())
    {
        state.storage_failure = error;
    }

    const auto found = std::find_if(state.queue.begin(), state.queue.end(),
                                    [&](const auto& operation)
                                    {
                                        return state.storage_failure ||
                                               operation->options.cancellation.stop_requested();
                                    });
    if (found == state.queue.end())
    {
        return {};
    }

    auto operation = std::move(*found);
    state.queue.erase(found);
    return operation;
}

/// @brief Pop an eligible FIFO request under admission synchronization.
std::shared_ptr<read_operation> take_ready(streamer_state& state)
{
    const std::lock_guard lock{state.mutex};
    if (state.queue.empty() || state.storage_failure)
    {
        return {};
    }

    auto operation = std::move(state.queue.front());
    state.queue.pop_front();
    return operation;
}
} // namespace

/// @brief Fill bounded native slots, observe cancellation wakeups and dispatch only terminal
/// results.
void streamer_state::run_io()
{
    std::size_t active{0};
    while (true)
    {
        while (const auto rejected = take_rejected(*this))
        {
            const auto error = rejected->options.cancellation.stop_requested()
                                   ? std::make_exception_ptr(jobs::job_cancelled{})
                                   : storage_failure;
            schedule(rejected, error);
        }

        while (active < limits.max_io_requests)
        {
            auto operation = take_ready(*this);
            if (!operation)
            {
                break;
            }

            if (operation->options.cancellation.stop_requested())
            {
                schedule(operation, std::make_exception_ptr(jobs::job_cancelled{}));
                continue;
            }

            ++active;
            io_in_flight.store(active);
            peak_io_in_flight.store(std::max(active, peak_io_in_flight.load()));
            try
            {
                const auto& source =
                    std::get<loose_file_asset>(catalog.at(operation->request.asset_id));
                auto ready = files->start(operation, source.path);
                if (ready)
                {
                    --active;
                    io_in_flight.store(active);
                    schedule(operation, std::move(*ready));
                }
            }
            catch (...)
            {
                --active;
                io_in_flight.store(active);
                schedule(operation, std::current_exception());
            }
        }

        if (active == 0)
        {
            const std::lock_guard lock{mutex};
            if ((!accepting || storage_failure) && queue.empty())
            {
                return;
            }
        }

        if (auto completion = files->poll(true))
        {
            --active;
            io_in_flight.store(active);
            schedule(completion->operation, std::move(completion->result));
        }
    }
}
} // namespace atlas::streaming::detail
