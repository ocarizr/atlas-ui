/** @file @brief Canonical persistent namespace, ordinal and type serialization. */
#include <algorithm>
#include <atlas/assets/catalog.hpp>
#include <limits>
#include <stdexcept>

namespace atlas::assets
{
namespace
{
constexpr std::string_view digits = "0123456789abcdef"; ///< Canonical lowercase hexadecimal digits.
constexpr auto types = std::to_array<std::string_view>(
    {"ui", "view_model", "flow", "script", "texture", "shader", "material", "video", "audio",
     "localization"}); ///< Stable v1 vocabulary in enumeration order.

/// @brief Reject uppercase, whitespace and nonhex input before decoding.
std::uint8_t nibble(char digit)
{
    const auto index = digits.find(digit);
    if (index == std::string_view::npos)
    {
        throw std::invalid_argument("Asset identity requires lowercase hexadecimal digits");
    }

    return static_cast<std::uint8_t>(index);
}

/// @brief The all-zero namespace is reserved for an uninitialized identity.
void require_namespace(const namespace_id& id)
{
    if (std::ranges::all_of(id.bytes,
                            [](auto value)
                            {
                                return value == 0;
                            }))
    {
        throw std::invalid_argument("Asset namespace must be nonzero");
    }
}

/// @brief Reserve both the zero sentinel and the exhausted allocation watermark.
void require_ordinal(std::uint64_t local)
{
    if (!local || local == std::numeric_limits<std::uint64_t>::max())
    {
        throw std::invalid_argument("Asset ordinal is reserved");
    }
}
} // namespace

/// @brief Decode one exact fixed-width project namespace.
namespace_id parse_namespace(std::string_view text)
{
    namespace_id result;
    if (text.size() != result.bytes.size() * 2)
    {
        throw std::invalid_argument("Asset namespace requires exactly 32 hex digits");
    }

    for (std::size_t i = 0; i < result.bytes.size(); ++i)
    {
        result.bytes[i] =
            static_cast<std::uint8_t>((nibble(text[i * 2]) << 4) | nibble(text[i * 2 + 1]));
    }

    require_namespace(result);
    return result;
}

/// @brief Encode bytes in canonical order without locale or host-endianness dependence.
std::string format_namespace(namespace_id id)
{
    require_namespace(id);
    std::string text;
    text.reserve(id.bytes.size() * 2);
    for (const auto byte : id.bytes)
    {
        text += digits[byte >> 4];
        text += digits[byte & 0xf];
    }

    return text;
}

/// @brief Decode the exact namespace and unsigned ordinal persisted by the project catalog.
asset_id parse_id(std::string_view text)
{
    if (text.size() != 49 || text[32] != ':')
    {
        throw std::invalid_argument("Invalid persistent asset ID shape");
    }

    const auto space = parse_namespace(text.substr(0, 32));
    std::uint64_t local = 0;
    for (const auto digit : text.substr(33))
    {
        local = (local << 4) | nibble(digit);
    }

    require_ordinal(local);
    return {space, local};
}

/// @brief Encode the project and ordinal with leading zeroes and no revision or path.
std::string format_id(asset_id id)
{
    require_ordinal(id.local);
    auto text = format_namespace(id.space);
    text += ':';
    for (int shift = 60; shift >= 0; shift -= 4)
    {
        text += digits[(id.local >> shift) & 0xf];
    }

    return text;
}

/// @brief Read the persisted vocabulary without guessing from file extensions.
asset_type parse_type(std::string_view name)
{
    const auto match = std::ranges::find(types, name);
    if (match == types.end())
    {
        throw std::invalid_argument("Unknown asset type: " + std::string{name});
    }

    return static_cast<asset_type>(match - types.begin());
}

/// @brief Reject invalid enum values before serializing or compiling a catalog.
std::string_view format_type(asset_type type)
{
    const auto index = static_cast<std::size_t>(type);
    if (index >= types.size())
    {
        throw std::invalid_argument("Unknown asset type value");
    }

    return types[index];
}

/// @brief Preserve the allocation watermark across deleted records and failed authoring attempts.
asset_id allocate_id(catalog_document& draft)
{
    require_namespace(draft.space);
    if (draft.next_local == std::numeric_limits<std::uint64_t>::max())
    {
        throw std::overflow_error("Asset identity namespace exhausted");
    }

    if (!draft.next_local ||
        std::ranges::any_of(draft.records,
                            [&](const auto& record)
                            {
                                return record.reference.id.space != draft.space ||
                                       record.reference.id.local >= draft.next_local;
                            }))
    {
        throw std::invalid_argument("Asset allocation watermark does not cover existing records");
    }

    return {draft.space, draft.next_local++};
}
} // namespace atlas::assets
