/** @file
 * @brief Fixed platform-neutral file interface implemented by exactly one selected backend.
 */
#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>

namespace atlas::streaming::detail
{
/// @brief Scoped storage handle used only on the streamer's dedicated I/O path.
class file_reader
{
  public:
    /// @brief Close the selected backend's handle without exposing native SDK types.
    virtual ~file_reader() = default;
    /// @brief Return the validated nonnegative source length captured when opening.
    [[nodiscard]] virtual std::uint64_t size() const noexcept = 0;
    /// @brief Fill the entire destination or throw; partial buffers are never published.
    virtual void read(std::uint64_t offset, std::span<std::uint8_t> destination) = 0;
};

/// @brief Open a registered path through the CMake-selected native or SDK-free backend.
std::unique_ptr<file_reader> open_file(const std::filesystem::path& path);
} // namespace atlas::streaming::detail
