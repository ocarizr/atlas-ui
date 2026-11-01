/**
 * @file
 * @brief Exact-range source validation and immutable byte acquisition.
 */

#pragma once

#include "buffer_memory.hpp"
#include <atlas/streaming/asset_streamer.hpp>

namespace atlas::streaming::detail
{
/// @brief Reject overflowing or out-of-source exact ranges before allocating payload storage.
void validate_range(std::uint64_t length, const read_request& request);
/// @brief Acquire one exact file range on the dedicated I/O path through the selected platform
/// adapter.
read_result read_file_asset(const loose_file_asset& source, const read_request& request,
                            const std::shared_ptr<buffer_reservation>& reservation) noexcept;
} // namespace atlas::streaming::detail
