/** @file @brief Shared limits for validated v1 metadata and its XML representation. */
#pragma once
#include <cstddef>

namespace atlas::assets::detail
{
inline constexpr std::size_t max_records = 4096;        ///< Maximum owned records.
inline constexpr std::size_t max_record_links = 64;     ///< Required and derived edges per record.
inline constexpr std::size_t max_catalog_links = 16384; ///< All authored links combined.
inline constexpr std::size_t max_xml_bytes = std::size_t{8} * 1024 * 1024; ///< Bound XML input.
} // namespace atlas::assets::detail
