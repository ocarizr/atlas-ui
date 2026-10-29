#pragma once

#include <atlas/streaming/asset_streamer.hpp>

namespace atlas::streaming::detail
{
read_result read_asset(const asset_catalog& catalog, const read_request& request) noexcept;
} // namespace atlas::streaming::detail
