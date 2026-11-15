/**
 * @file
 * @brief Version and integrity envelope shared by prepared document formats.
 */
#pragma once
#include <atlas/mvvm_content/detail/prepared_binary.hpp>

namespace atlas::mvvm_content::detail
{
using signature = std::array<std::byte, 4>;             ///< Standalone prepared document kind.
inline constexpr std::size_t prepared_header_size = 14; ///< Signature, version, size, checksum.

/// @brief Detect accidental payload corruption before admitting decoded records.
inline std::uint32_t prepared_checksum(std::span<const std::byte> bytes)
{
    std::uint32_t value = 2166136261U;
    for (auto byte : bytes)
    {
        value = (value ^ std::to_integer<std::uint32_t>(byte)) * 16777619U;
    }

    return value;
}

/// @brief Wrap one bounded typed payload in its kind and wire version.
inline std::vector<std::byte> seal_prepared(binary<false>& payload, signature magic,
                                            std::uint16_t version)
{
    if (payload.output.size() > max_prepared_bytes - prepared_header_size)
    {
        throw std::length_error("Prepared content exceeds its byte budget");
    }

    auto size = static_cast<std::uint32_t>(payload.output.size());
    auto integrity = prepared_checksum(payload.output);
    binary<false> header;
    header.raw(magic);
    header.fields(version, size, integrity);
    header.raw(payload.output);
    return std::move(header.output);
}

/// @brief Require a complete matching, intact envelope before interpreting its byte payload.
inline std::span<const std::byte> open_prepared(std::span<const std::byte> bytes, signature magic,
                                                std::uint16_t version)
{
    if (bytes.size() < prepared_header_size || bytes.size() > max_prepared_bytes)
    {
        throw std::invalid_argument("Invalid prepared content envelope size");
    }

    binary<true> header{bytes.first(prepared_header_size)};
    signature actual{};
    std::uint16_t encoded_version = 0;
    std::uint32_t size = 0;
    std::uint32_t integrity = 0;
    header.raw(actual);
    header.fields(encoded_version, size, integrity);
    if (actual != magic || encoded_version != version)
    {
        throw std::invalid_argument("Unsupported prepared content signature or version");
    }

    const auto payload = bytes.subspan(prepared_header_size);
    if (size != payload.size() || integrity != prepared_checksum(payload))
    {
        throw std::invalid_argument("Invalid prepared content extent or checksum");
    }

    return payload;
}
} // namespace atlas::mvvm_content::detail
