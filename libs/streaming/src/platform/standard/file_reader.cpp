/** @file
 * @brief SDK-free reference file backend for portable-library validation on Windows.
 */
#include "file_reader.hpp"
#include <fstream>
#include <stdexcept>

namespace atlas::streaming::detail
{
/// @brief Standard-library handle confined to the dedicated I/O thread.
class standard_file_reader final : public file_reader
{
  public:
    /// @brief Open the registered source and determine its length.
    explicit standard_file_reader(const std::filesystem::path& path)
        : file_{path, std::ios::binary | std::ios::ate}
    {
        if (!file_ || file_.tellg() < 0)
        {
            throw std::runtime_error{"Cannot open or determine the registered asset length"};
        }

        size_ = static_cast<std::uint64_t>(file_.tellg());
    }

    /// @brief Return the captured source length.
    std::uint64_t size() const noexcept override
    {
        return size_;
    }

    /// @brief Read exactly the validated range or reject partial bytes.
    void read(std::uint64_t offset, std::span<std::uint8_t> destination) override
    {
        file_.seekg(static_cast<std::streamoff>(offset));
        if (!destination.empty())
        {
            file_.read(reinterpret_cast<char*>(destination.data()),
                       static_cast<std::streamsize>(destination.size()));
        }

        if (!file_)
        {
            throw std::runtime_error{"Asset read did not complete; partial data was discarded"};
        }
    }

  private:
    /// @brief Owned input stream, never accessed by CPU workers.
    std::ifstream file_;
    /// @brief Captured nonnegative file length.
    std::uint64_t size_{0};
};

/// @brief Select the SDK-free reference implementation through the same storage interface.
std::unique_ptr<file_reader> open_file(const std::filesystem::path& path)
{
    return std::make_unique<standard_file_reader>(path);
}
} // namespace atlas::streaming::detail
