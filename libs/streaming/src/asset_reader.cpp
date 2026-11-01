/**
 * @file
 * @brief Exact-range source validation and immutable byte acquisition.
 */

#include "asset_reader.hpp"

#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace atlas::streaming::detail
{
namespace
{
/// @brief Reject overflowing or out-of-source ranges before copying or allocating a result.
void validate_range(std::uint64_t length, const read_request& request)
{
    if (request.offset > length || request.size > length - request.offset)
    {
        throw std::out_of_range{"Asset read exceeds the source range"};
    }
}

/// @brief Copy a validated asset-relative range from pinned immutable memory.
std::shared_ptr<const asset_bytes> read_memory(const memory_asset& source,
                                               const read_request& request)
{
    if (!source.bytes)
    {
        throw std::invalid_argument{"Memory asset has no backing bytes"};
    }

    validate_range(source.bytes->size(), request);
    auto output = std::make_shared<asset_bytes>(request.size);
    if (request.size != 0)
    {
        std::copy_n(source.bytes->begin() + static_cast<std::ptrdiff_t>(request.offset),
                    request.size, output->begin());
    }

    return output;
}

/// @brief Read an exact authorized file range on the dedicated I/O path; never publish partial
/// bytes.
std::shared_ptr<const asset_bytes> read_file(const loose_file_asset& source,
                                             const read_request& request)
{
    std::ifstream file{source.path, std::ios::binary | std::ios::ate};
    if (!file)
    {
        throw std::runtime_error{"Cannot open the registered asset file"};
    }

    const auto end = static_cast<std::streamoff>(file.tellg());
    if (end < 0)
    {
        throw std::runtime_error{"Cannot determine the asset length"};
    }

    validate_range(static_cast<std::uint64_t>(end), request);
    file.seekg(static_cast<std::streamoff>(request.offset));
    auto output = std::make_shared<asset_bytes>(request.size);
    if (request.size != 0)
    {
        file.read(reinterpret_cast<char*>(output->data()),
                  static_cast<std::streamsize>(request.size));
    }

    if (!file)
    {
        throw std::runtime_error{"Asset read did not complete; partial data was discarded"};
    }

    return output;
}
} // namespace

/// @brief Resolve a registered source and return immutable owned bytes or the original read error.
read_result read_asset(const asset_catalog& catalog, const read_request& request) noexcept
{
    try
    {
        const auto found = catalog.find(request.asset_id);
        if (found == catalog.end())
        {
            throw std::out_of_range{"Asset ID is not registered"};
        }

        if (const auto* memory = std::get_if<memory_asset>(&found->second))
        {
            return read_memory(*memory, request);
        }

        return read_file(std::get<loose_file_asset>(found->second), request);
    }
    catch (...)
    {
        return std::current_exception();
    }
}
} // namespace atlas::streaming::detail
