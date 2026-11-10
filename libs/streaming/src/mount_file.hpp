/** @file @brief Shared file metadata probe for loose and prepared mount validation. */
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>

namespace atlas::streaming::detail
{
/// @brief Return the length of a readable file already checked to be regular.
[[nodiscard]] std::optional<std::uint64_t> mount_file_extent(const std::filesystem::path& path);
} // namespace atlas::streaming::detail
