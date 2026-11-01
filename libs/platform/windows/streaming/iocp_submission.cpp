/** @file @brief Exception-safe native admission and exact-range chunk submission. */
#include "asset_reader.hpp"
#include "iocp_state.hpp"
#include <algorithm>
#include <system_error>

namespace atlas::streaming::detail
{
/// @brief Retain logical ownership before opening the native identity.
iocp_read::iocp_read(iocp_api& calls, std::shared_ptr<read_operation> work,
                     const std::filesystem::path& path)
    : api{calls}, operation{std::move(work)}, file{api.open_file(path)}
{
}

/// @brief The backend guarantees that the kernel no longer references this request.
iocp_read::~iocp_read()
{
    api.close(file);
}

/// @brief Preserve native diagnostics, including allocation failure while constructing an error.
std::exception_ptr io_error(DWORD error) noexcept
{
    try
    {
        throw std::system_error{static_cast<int>(error), std::system_category(),
                                "Overlapped asset read"};
    }
    catch (...)
    {
        return std::current_exception();
    }
}

/// @brief Perform every potentially allocating step before calling ReadFile.
std::optional<read_result> iocp_backend::start(const std::shared_ptr<read_operation>& operation,
                                               const std::filesystem::path& path)
{
    if (failure_)
    {
        std::rethrow_exception(failure_);
    }

    if (operation->options.cancellation.stop_requested())
    {
        throw jobs::job_cancelled{};
    }

    auto request = std::make_unique<iocp_read>(*api_, operation, path);
    validate_range(api_->file_size(request->file), operation->request);
    request->output = allocate_buffer(operation->reservation);
    if (operation->options.cancellation.stop_requested())
    {
        throw jobs::job_cancelled{};
    }

    if (operation->request.size == 0)
    {
        return read_result{std::shared_ptr<const asset_bytes>{std::move(request->output)}};
    }

    api_->associate(request->file, port_);
    auto* identity = &request->native;
    auto [entry, inserted] = active_.emplace(identity, std::move(request));
    (void)inserted;
    const auto status = issue(*entry->second);
    if (status != ERROR_SUCCESS && status != ERROR_IO_PENDING)
    {
        active_.erase(entry);
        std::rethrow_exception(io_error(status));
    }

    return std::nullopt;
}

/// @brief Reuse OVERLAPPED only after its previous packet was consumed.
DWORD iocp_backend::issue(iocp_read& request) noexcept
{
    request.native = {};
    const auto offset = request.operation->request.offset + request.progress;
    request.native.Offset = static_cast<DWORD>(offset);
    request.native.OffsetHigh = static_cast<DWORD>(offset >> 32);
    request.transfer = std::min(request.output->size() - request.progress, transfer_limit_);
    const auto status = api_->read(
        request.file, std::span{*request.output}.subspan(request.progress, request.transfer),
        request.native);
    request.pending = status == ERROR_SUCCESS || status == ERROR_IO_PENDING;
    return status;
}

/// @brief Cancellation is a request, never proof that native ownership ended.
void iocp_backend::cancel(iocp_read& request) noexcept
{
    if (request.cancellation_sent || !request.pending)
    {
        return;
    }

    request.cancellation_sent = true;
    ++cancellations_;
    const auto status = api_->cancel(request.file, request.native);
    if (status == ERROR_NOT_FOUND)
    {
        ++races_;
    }
    else if (status != ERROR_SUCCESS)
    {
        ++errors_;
    }
}
} // namespace atlas::streaming::detail
