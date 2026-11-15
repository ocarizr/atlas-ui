/** @file @brief Version and integrity envelope for typed UI binary content. */
#include "document_validation.hpp"
#include "prepared_ui_fields.hpp"
#include <atlas/mvvm_content/detail/prepared_definition.hpp>
#include <atlas/mvvm_content/detail/prepared_envelope.hpp>

namespace atlas::ui_content
{
namespace
{
/// @brief Atlas UI prepared signature.
constexpr mvvm_content::detail::signature magic{std::byte{'A'}, std::byte{'U'}, std::byte{'I'},
                                                std::byte{'P'}};
constexpr std::uint16_t wire_version = 2; ///< Add the embedded ViewModel definition table.

/// @brief Encode the typed UI and its exact catalog closure in v2 field order.
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
    format.sequence(source.models, detail::max_models,
                    [](auto& archive, prepared_model_record& record)
                    {
                        detail::reference(archive, record.source);
                        mvvm::definition definition;
                        if constexpr (!Reading)
                        {
                            if (!record.value)
                            {
                                throw std::invalid_argument("Missing prepared ViewModel schema");
                            }

                            definition = record.value->describe();
                        }

                        mvvm_content::detail::prepared_definition(archive, definition);
                        if constexpr (Reading)
                        {
                            record.value = mvvm::compile(definition);
                        }
                    });
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
    return mvvm_content::detail::seal_prepared(payload, magic, wire_version);
}

/// @brief Reject incorrect extent, version and checksum before decoding typed records.
prepared_view decode_prepared(std::span<const std::byte> bytes)
{
    detail::binary<true> archive{mvvm_content::detail::open_prepared(bytes, magic, wire_version)};
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
