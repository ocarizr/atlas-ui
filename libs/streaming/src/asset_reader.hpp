/**
 * @file
 * @brief Exact-range source validation and immutable byte acquisition.
 */

#pragma once

#include <atlas/streaming/asset_streamer.hpp>

namespace atlas::streaming::detail
{
/// @brief Resolve a registered source and return immutable owned bytes or the original read error.
read_result read_asset(const asset_catalog& catalog, const read_request& request) noexcept;
} // namespace atlas::streaming::detail
