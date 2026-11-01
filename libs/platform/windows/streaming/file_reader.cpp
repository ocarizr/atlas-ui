/** @file
 * @brief Native Windows storage handle; SDK types remain below the platform boundary.
 */
#include "file_reader.hpp"
#include <Windows.h>
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace atlas::streaming::detail
{
/// @brief Windows file handle closed before prepared bytes reach CPU jobs.
class windows_file_reader final : public file_reader
{
  public:
    /// @brief Open a registered path and capture a representable source length.
    explicit windows_file_reader(const std::filesystem::path& path)
    {
        handle_ = CreateFileW(path.c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE)
        {
            throw std::runtime_error{"Cannot open the registered asset file"};
        }

        LARGE_INTEGER length{};
        if (!GetFileSizeEx(handle_, &length) || length.QuadPart < 0)
        {
            CloseHandle(handle_);
            throw std::runtime_error{"Cannot determine the asset length"};
        }

        size_ = static_cast<std::uint64_t>(length.QuadPart);
    }

    /// @brief Release the native handle after acquisition, including failed reads.
    ~windows_file_reader() override
    {
        CloseHandle(handle_);
    }

    /// @brief Native handles have unique ownership.
    windows_file_reader(const windows_file_reader&) = delete;
    /// @brief Native handles cannot be copied or overwritten.
    windows_file_reader& operator=(const windows_file_reader&) = delete;

    /// @brief Return the captured source length.
    std::uint64_t size() const noexcept override
    {
        return size_;
    }

    /// @brief Perform exact reads on the dedicated I/O thread without blocking CPU workers.
    void read(std::uint64_t offset, std::span<std::uint8_t> destination) override
    {
        LARGE_INTEGER position{};
        position.QuadPart = static_cast<LONGLONG>(offset);
        if (!SetFilePointerEx(handle_, position, nullptr, FILE_BEGIN))
        {
            throw std::runtime_error{"Cannot seek the registered asset file"};
        }

        while (!destination.empty())
        {
            const auto requested = static_cast<DWORD>(std::min(
                destination.size(), static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
            DWORD received{0};
            if (!ReadFile(handle_, destination.data(), requested, &received, nullptr) ||
                received != requested)
            {
                throw std::runtime_error{"Asset read did not complete; partial data was discarded"};
            }

            destination = destination.subspan(received);
        }
    }

  private:
    /// @brief Exclusive native handle owned by this scoped adapter.
    HANDLE handle_{INVALID_HANDLE_VALUE};
    /// @brief Captured nonnegative source length.
    std::uint64_t size_{0};
};

/// @brief Select the scoped native implementation without exposing Windows SDK types.
std::unique_ptr<file_reader> open_file(const std::filesystem::path& path)
{
    return std::make_unique<windows_file_reader>(path);
}
} // namespace atlas::streaming::detail
