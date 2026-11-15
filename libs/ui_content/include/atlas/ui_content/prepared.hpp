/**
 * @file
 * @brief Versioned prepared UI and material descriptors.
 */
#pragma once
#include <atlas/ui_content/instance.hpp>

namespace atlas::ui_content
{
/// @brief Exact authored revision required in the runtime catalog.
struct prepared_dependency
{
    assets::asset_ref source;   ///< Stable typed identity.
    std::uint64_t revision = 0; ///< Positive authored revision.
};

/// @brief Owned material descriptor included in a prepared view.
struct prepared_material_record
{
    assets::asset_ref source; ///< Material identity also present in the dependency table.
    material_document value;  ///< Validated template or sparse override.
};

/// @brief Typed intermediate representation independent of XML and backing storage.
struct prepared_view
{
    assets::asset_ref source;                        ///< Original UI asset identity.
    document view;                                   ///< Ordered view authoring data.
    std::vector<prepared_dependency> dependencies;   ///< Sorted closure including the view itself.
    std::vector<prepared_material_record> materials; ///< Sorted transitive material descriptors.
};

/// @brief Prepare the current XML UI revision and its material closure from a catalog mount.
[[nodiscard]] prepared_view prepare_view(assets::asset_ref source, const construction& options);
/// @brief Encode the typed data as bounded, deterministic v1 bytes.
[[nodiscard]] std::vector<std::byte> encode_prepared(const prepared_view& source);
/// @brief Validate and decode a complete prepared payload without parsing XML.
[[nodiscard]] prepared_view decode_prepared(std::span<const std::byte> bytes);
/// @brief Read a prepared UI record through the current storage mount.
[[nodiscard]] prepared_view load_prepared(assets::asset_ref source, const construction& options);
/// @brief Check mounted revisions and construct a private runtime instance.
[[nodiscard]] std::unique_ptr<instance> build_prepared(const prepared_view& source,
                                                       const construction& options);
} // namespace atlas::ui_content
