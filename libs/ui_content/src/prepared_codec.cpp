/** @file @brief Version and integrity envelope for typed UI binary content. */
#include "document_validation.hpp"
#include "prepared_ui_fields.hpp"

namespace atlas::ui_content
{
namespace
{
/// @brief Atlas UI prepared v1 signature.
constexpr std::array magic{std::byte{'A'}, std::byte{'U'}, std::byte{'I'}, std::byte{'P'}};
constexpr std::uint16_t wire_version = 1; ///< Schema and byte-order contract.
constexpr std::size_t header_size = 14;   ///< Four signature bytes, version, size and checksum.

/// @brief Detect accidental payload corruption before admitting any decoded records.
std::uint32_t checksum(std::span<const std::byte> bytes)
{
    std::uint32_t value = 2166136261U;
    for (auto byte : bytes)
    {
        value = (value ^ std::to_integer<std::uint32_t>(byte)) * 16777619U;
    }

    return value;
}

/// @brief Encode the typed UI and its exact catalog closure in v1 field order.
template <bool Reading> void serialize(detail::binary<Reading>& format, prepared_view& source)
{
    detail::reference(format, source.source);
    format.sequence(source.dependencies, detail::max_prepared_dependencies,
                    [](auto& archive, prepared_dependency& entry)
                    {
                        detail::reference(archive, entry.source);
                        archive.field(entry.revision);
                    });
    format.sequence(source.materials, detail::max_prepared_materials,
                    detail::material_record_fields);
    format.sequence(source.view.models, detail::max_models,
                    [](auto& archive, model_context& model)
                    {
                        archive.field(model.name);
                        detail::reference(archive, model.source);
                    });
    format.sequence(source.view.elements, detail::max_elements, detail::prepared_element_fields);
}
} // namespace

/// @brief Write a deterministic envelope around the validated prepared descriptors.
std::vector<std::byte> encode_prepared(const prepared_view& source)
{
    detail::validate_prepared(source);
    auto copy = source;
    detail::binary<false> payload;
    serialize(payload, copy);
    if (payload.output.size() > detail::max_prepared_bytes - header_size)
    {
        throw std::length_error("Prepared UI exceeds its byte budget");
    }

    auto signature = magic;
    auto version = wire_version;
    auto size = static_cast<std::uint32_t>(payload.output.size());
    auto integrity = checksum(payload.output);
    detail::binary<false> header;
    header.raw(signature);
    header.fields(version, size, integrity);
    header.raw(payload.output);
    return std::move(header.output);
}

/// @brief Reject incorrect extent, version and checksum before decoding typed records.
prepared_view decode_prepared(std::span<const std::byte> bytes)
{
    if (bytes.size() < header_size || bytes.size() > detail::max_prepared_bytes)
    {
        throw std::invalid_argument("Invalid prepared UI envelope size");
    }

    detail::binary<true> header{bytes.first(header_size)};
    std::array<std::byte, magic.size()> signature{};
    std::uint16_t version = 0;
    std::uint32_t size = 0;
    std::uint32_t integrity = 0;
    header.raw(signature);
    header.fields(version, size, integrity);
    if (signature != magic || version != wire_version)
    {
        throw std::invalid_argument("Unsupported prepared UI signature or version");
    }

    const auto payload = bytes.subspan(header_size);
    if (size != payload.size() || integrity != checksum(payload))
    {
        throw std::invalid_argument("Invalid prepared UI extent or checksum");
    }

    detail::binary<true> archive{payload};
    prepared_view result;
    serialize(archive, result);
    if (!archive.exhausted())
    {
        throw std::invalid_argument("Trailing prepared UI payload");
    }

    detail::validate_prepared(result);
    return result;
}
} // namespace atlas::ui_content
