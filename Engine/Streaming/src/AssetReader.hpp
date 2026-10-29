#pragma once

#include <atlas/streaming/AssetStreamer.hpp>

namespace atlas::streaming::detail
{
ReadResult read_asset(const AssetCatalog& catalog, const ReadRequest& request) noexcept;
} // namespace atlas::streaming::detail
