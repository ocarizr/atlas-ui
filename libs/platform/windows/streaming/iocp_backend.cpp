/** @file @brief Completion-driven ownership, cancellation and fatal-port recovery. */
#include "iocp_state.hpp"
#include <stdexcept>

namespace atlas::streaming::detail
{
/// @brief Allocate the port only after validating the private transfer policy.
iocp_backend::iocp_backend(std::shared_ptr<iocp_api> api, std::size_t transfer_limit)
    : api_{std::move(api)}, transfer_limit_{transfer_limit}
{
    if (!api_ || transfer_limit == 0 || transfer_limit > MAXDWORD)
    {
        throw std::invalid_argument{"Invalid IOCP adapter or transfer limit"};
    }

    port_ = api_->create_port();
}

/// @brief Never destroy an OVERLAPPED or payload still referenced by Windows.
iocp_backend::~iocp_backend()
{
    drain();
    active_.clear();
    api_->close(port_);
}

/// @brief Emergency waits stay on the storage owner, never a CPU worker.
void iocp_backend::drain() noexcept
{
    for (auto& [identity, request] : active_)
    {
        (void)identity;
        if (request->pending)
        {
            cancel(*request);
        }
    }

    for (auto& [identity, request] : active_)
    {
        (void)identity;
        if (request->pending)
        {
            api_->finish(request->file, request->native);
            request->pending = false;
        }
    }
}

/// @brief Finalize only after the kernel releases the current chunk.
file_completion iocp_backend::complete(OVERLAPPED* identity, std::exception_ptr error) noexcept
{
    auto node = active_.extract(identity);
    auto& request = *node.mapped();
    read_result result =
        error ? read_result{std::move(error)}
              : read_result{std::shared_ptr<const asset_bytes>{std::move(request.output)}};
    return {std::move(request.operation), std::move(result)};
}

/// @brief Consume packets including synchronous-success submissions, exactly once.
std::optional<file_completion> iocp_backend::poll(bool block) noexcept
{
    if (failure_)
    {
        if (active_.empty())
        {
            return std::nullopt;
        }

        return complete(active_.begin()->first, failure_);
    }

    for (auto& [identity, request] : active_)
    {
        (void)identity;
        if (request->operation->options.cancellation.stop_requested())
        {
            cancel(*request);
        }
    }

    const auto packet = api_->wait(port_, block);
    if (!packet.overlapped)
    {
        if (packet.error != ERROR_SUCCESS && packet.error != WAIT_TIMEOUT)
        {
            failure_ = io_error(packet.error);
            drain();
            if (!active_.empty())
            {
                return complete(active_.begin()->first, failure_);
            }
        }

        return std::nullopt;
    }

    const auto found = active_.find(packet.overlapped);
    if (found == active_.end())
    {
        return std::nullopt;
    }

    auto& request = *found->second;
    request.pending = false;
    if (request.operation->options.cancellation.stop_requested() ||
        packet.error == ERROR_OPERATION_ABORTED)
    {
        try
        {
            throw jobs::job_cancelled{};
        }
        catch (...)
        {
            return complete(packet.overlapped, std::current_exception());
        }
    }

    if (packet.error != ERROR_SUCCESS)
    {
        return complete(packet.overlapped, io_error(packet.error));
    }

    if (packet.bytes != request.transfer)
    {
        return complete(packet.overlapped, io_error(ERROR_HANDLE_EOF));
    }

    request.progress += packet.bytes;
    if (request.progress != request.output->size())
    {
        const auto status = issue(request);
        if (status != ERROR_SUCCESS && status != ERROR_IO_PENDING)
        {
            return complete(packet.overlapped, io_error(status));
        }

        return std::nullopt;
    }

    return complete(packet.overlapped, {});
}

/// @brief Counters describe attempts and races, not guaranteed cancellation wins.
file_io_statistics iocp_backend::statistics() const noexcept
{
    return {cancellations_.load(), races_.load(), errors_.load()};
}

/// @brief Construct the private backend with an injectable native-call adapter.
std::unique_ptr<file_io> make_iocp_backend(std::shared_ptr<iocp_api> api,
                                           std::size_t transfer_limit)
{
    return std::make_unique<iocp_backend>(std::move(api), transfer_limit);
}
} // namespace atlas::streaming::detail
