/**
 * @file
 * @brief Versioned prepared UI and material descriptors.
 */
#pragma once
#include <atlas/ui_content/flow.hpp>

namespace atlas::ui_content
{
/// @brief Exact authored revision required in the runtime catalog.
struct prepared_dependency
{
    assets::asset_ref source;                                    ///< Stable typed identity.
    std::uint64_t revision = 0;                                  ///< Positive authored revision.
    bool operator==(const prepared_dependency&) const = default; ///< Exact revision match.
};

/// @brief Owned material descriptor included in a prepared view.
struct prepared_material_record
{
    assets::asset_ref source; ///< Material identity also present in the dependency table.
    material_document value;  ///< Validated template or sparse override.
};

/// @brief Immutable ViewModel schema whose identity and revision remain catalog-backed.
struct prepared_model_record
{
    assets::asset_ref source;          ///< Shared source for any number of named model contexts.
    std::optional<mvvm::schema> value; ///< Validated schema owning fields and signal protocols.
};

/// @brief Typed intermediate representation independent of XML and backing storage.
struct prepared_view
{
    assets::asset_ref source;                        ///< Original UI asset identity.
    document view;                                   ///< Ordered view authoring data.
    std::vector<prepared_dependency> dependencies;   ///< Sorted closure including the view itself.
    std::vector<prepared_material_record> materials; ///< Sorted transitive material descriptors.
    std::vector<prepared_model_record> models;       ///< Sorted unique ViewModel definitions.
};

/// @brief Prepare the current XML UI revision and its material closure from a catalog mount.
[[nodiscard]] prepared_view prepare_view(assets::asset_ref source, const construction& options);
/// @brief Encode the typed data as bounded, deterministic prepared bytes.
[[nodiscard]] std::vector<std::byte> encode_prepared(const prepared_view& source);
/// @brief Validate and decode a complete prepared payload without parsing XML.
[[nodiscard]] prepared_view decode_prepared(std::span<const std::byte> bytes);
/// @brief Read a prepared UI record through the current storage mount.
[[nodiscard]] prepared_view load_prepared(assets::asset_ref source, const construction& options);
/// @brief Check mounted revisions and construct a private runtime instance.
[[nodiscard]] std::unique_ptr<instance> build_prepared(const prepared_view& source,
                                                       const construction& options);

/// @brief Dependency contract for one distinct separately prepared screen.
struct prepared_flow_screen
{
    assets::asset_ref source;                      ///< Stable UI asset identity.
    std::vector<prepared_dependency> dependencies; ///< Exact closure of its prepared UI record.
};

/// @brief Prepared Flow graph and catalog bindings, independent of physical storage.
struct prepared_flow
{
    prepared_dependency source; ///< Original Flow asset identity and revision.
    flow_document graph;        ///< Validated authoring graph used by the normal runtime.
    std::vector<prepared_flow_screen> screens; ///< Sorted distinct UI dependency contracts.
};

/// @brief Read the authored graph and prepare the exact dependency contract for every screen.
[[nodiscard]] prepared_flow prepare_flow(assets::asset_ref source, const construction& options);
/// @brief Collect the sorted unique Flow, screen and transitive dependency revisions.
[[nodiscard]] std::vector<prepared_dependency> flow_dependencies(const prepared_flow& source);
/// @brief Encode a validated graph and its screen contracts with a version and checksum.
[[nodiscard]] std::vector<std::byte> encode_prepared(const prepared_flow& source);
/// @brief Decode a binary graph without XML parsing.
[[nodiscard]] prepared_flow decode_prepared_flow(std::span<const std::byte> bytes);
/// @brief Read and verify a typed binary Flow asset and its mounted dependency revisions.
[[nodiscard]] prepared_flow load_prepared_flow(assets::asset_ref source,
                                               const construction& options);
/// @brief Construct the normal Flow Manager with nonblocking prepared screen reads.
[[nodiscard]] std::unique_ptr<flow_manager> build_prepared_flow(const prepared_flow& source,
                                                                const construction& options);
} // namespace atlas::ui_content
