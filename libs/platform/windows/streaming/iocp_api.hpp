/** @file @brief Private Windows completion-port calls, replaceable by controlled tests. */
#pragma once
#include "file_io.hpp"
#include <Windows.h>
#include <span>

namespace atlas::streaming::detail
{
/// @brief One dequeued OS completion or a wake/timeout with no operation.
struct completion_packet
{
    /// @brief Stable native request identity; null for wake, timeout or port failure.
    OVERLAPPED* overlapped{};
    /// @brief Bytes transferred by this chunk.
    DWORD bytes{};
    /// @brief Win32 status; ERROR_SUCCESS denotes success.
    DWORD error{};
};

/// @brief Native calls kept behind a private seam for deterministic lifetime/cancellation tests.
class iocp_api
{
  public:
    /// @brief Release the adapter after its port and files are closed.
    virtual ~iocp_api() = default;
    /// @brief Create a completion port or throw before admission.
    virtual HANDLE create_port() = 0;
    /// @brief Open an overlapped file or throw before admission.
    virtual HANDLE open_file(const std::filesystem::path& path) = 0;
    /// @brief Query the current file size or throw before admission.
    virtual std::uint64_t file_size(HANDLE file) = 0;
    /// @brief Associate a file with this port or throw before submission.
    virtual void associate(HANDLE file, HANDLE port) = 0;
    /// @brief Submit a chunk; success still requires consuming its completion packet.
    virtual DWORD read(HANDLE file, std::span<std::uint8_t> bytes, OVERLAPPED& native) noexcept = 0;
    /// @brief Request cancellation without releasing OS ownership.
    virtual DWORD cancel(HANDLE file, OVERLAPPED& native) noexcept = 0;
    /// @brief Dequeue a completion, with a bounded watchdog when blocking.
    virtual completion_packet wait(HANDLE port, bool block) noexcept = 0;
    /// @brief Emergency drain used only after port failure or during destruction.
    virtual void finish(HANDLE file, OVERLAPPED& native) noexcept = 0;
    /// @brief Wake the I/O owner without running consumer code.
    virtual void wake(HANDLE port) noexcept = 0;
    /// @brief Close a native resource after the kernel releases its operations.
    virtual void close(HANDLE handle) noexcept = 0;
};

/// @brief Construct the backend; a small transfer limit is available only to private tests.
std::unique_ptr<file_io> make_iocp_backend(std::shared_ptr<iocp_api> api,
                                           std::size_t transfer_limit = MAXDWORD);
} // namespace atlas::streaming::detail
