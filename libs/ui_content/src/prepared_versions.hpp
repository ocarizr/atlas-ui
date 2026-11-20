/** @file @brief Peek only the bounded version field before validating the full envelope. */
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace atlas::ui_content::detail
{
/// @brief Signature, extent and checksum remain mandatory in open_prepared after this peek.
inline std::uint16_t prepared_version(std::span<const std::byte> bytes)
{
    if (bytes.size() < 6)
    {
        throw std::invalid_argument("Truncated prepared content version");
    }

    return static_cast<std::uint16_t>(std::to_integer<std::uint16_t>(bytes[4]) |
                                      (std::to_integer<std::uint16_t>(bytes[5]) << 8));
}
} // namespace atlas::ui_content::detail
