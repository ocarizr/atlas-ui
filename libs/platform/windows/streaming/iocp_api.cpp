/** @file @brief Windows overlapped-file and completion-port system calls. */
#include "iocp_api.hpp"
#include <system_error>

namespace atlas::streaming::detail
{
namespace
{
/// @brief Throw the saved Windows error without exposing SDK types in public headers.
[[noreturn]] void fail(const char* message)
{
    throw std::system_error{static_cast<int>(GetLastError()), std::system_category(), message};
}

/// @brief Thin production adapter; all request ownership belongs to the backend.
class windows_iocp_api final : public iocp_api
{
  public:
    /// @brief Create a port serviced by one persistent owner.
    HANDLE create_port() override
    {
        const auto port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);
        if (!port)
        {
            fail("CreateIoCompletionPort");
        }

        return port;
    }

    /// @brief Preserve loose-file replacement and sharing while enabling overlapped reads.
    HANDLE open_file(const std::filesystem::path& path) override
    {
        const auto file = CreateFileW(
            path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            fail("CreateFileW");
        }

        return file;
    }

    /// @brief Query the opened identity instead of trusting catalog-time metadata.
    std::uint64_t file_size(HANDLE file) override
    {
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(file, &size))
        {
            fail("GetFileSizeEx");
        }

        return static_cast<std::uint64_t>(size.QuadPart);
    }

    /// @brief Route every successful asynchronous submission to the selected port.
    void associate(HANDLE file, HANDLE port) override
    {
        if (!CreateIoCompletionPort(file, port, 0, 0))
        {
            fail("Associate completion port");
        }
    }

    /// @brief Return immediate submission status; never synthesize a success packet.
    DWORD read(HANDLE file, std::span<std::uint8_t> bytes, OVERLAPPED& native) noexcept override
    {
        return ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), nullptr, &native)
                   ? ERROR_SUCCESS
                   : GetLastError();
    }

    /// @brief Cancel precisely this operation; ERROR_NOT_FOUND is a completion race.
    DWORD cancel(HANDLE file, OVERLAPPED& native) noexcept override
    {
        return CancelIoEx(file, &native) ? ERROR_SUCCESS : GetLastError();
    }

    /// @brief A timeout also protects against a failed best-effort wake post.
    completion_packet wait(HANDLE port, bool block) noexcept override
    {
        completion_packet packet;
        ULONG_PTR key{};
        if (!GetQueuedCompletionStatus(port, &packet.bytes, &key, &packet.overlapped,
                                       block ? 1000 : 0))
        {
            packet.error = GetLastError();
        }

        return packet;
    }

    /// @brief Wait outside CPU workers until the kernel has stopped referencing the request.
    void finish(HANDLE file, OVERLAPPED& native) noexcept override
    {
        DWORD bytes{};
        GetOverlappedResult(file, &native, &bytes, TRUE);
    }

    /// @brief Post a control packet; the wait watchdog handles a failed post.
    void wake(HANDLE port) noexcept override
    {
        PostQueuedCompletionStatus(port, 0, 0, nullptr);
    }

    /// @brief Close resources only after the completion owner has drained them.
    void close(HANDLE handle) noexcept override
    {
        CloseHandle(handle);
    }
};
} // namespace

/// @brief Select the sole Windows storage backend.
std::unique_ptr<file_io> make_file_io()
{
    return make_iocp_backend(std::make_shared<windows_iocp_api>());
}
} // namespace atlas::streaming::detail
