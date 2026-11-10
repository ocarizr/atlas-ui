/**
 * @file
 * @brief Persistent typed asset identities and validated, read-only catalog snapshots.
 */
#pragma once
#include <array>
#include <atlas/diagnostics/instrumentation.hpp>
#include <compare>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace atlas::assets
{
/// @brief Nonzero project namespace provisioned once by the composition root.
struct namespace_id
{
    std::array<std::uint8_t, 16> bytes{}; ///< Fixed-width persistent namespace value.
    auto operator<=>(const namespace_id&) const = default; ///< Canonical byte ordering.
};

/// @brief Persistent identity independent of storage location, type, revision and residency.
struct asset_id
{
    namespace_id space;    ///< Owning project namespace.
    std::uint64_t local{}; ///< Positive ordinal allocated within the namespace.
    auto operator<=>(const asset_id&) const = default; ///< Namespace then ordinal.
};

/// @brief Parse exactly 32 lowercase hexadecimal digits for a nonzero namespace.
[[nodiscard]] namespace_id parse_namespace(std::string_view text);
/// @brief Serialize a namespace to its canonical lowercase hexadecimal form.
[[nodiscard]] std::string format_namespace(namespace_id id);
/// @brief Parse a namespace, colon and 16 lowercase hexadecimal digits for the ordinal.
[[nodiscard]] asset_id parse_id(std::string_view text);
/// @brief Serialize a valid persistent identity independently of any path or revision.
[[nodiscard]] std::string format_id(asset_id id);

/// @brief Stable v1 type vocabulary persisted using lowercase names.
enum class asset_type : std::uint8_t
{
    ui,          ///< Authored UI document.
    view_model,  ///< ViewModel document.
    flow,        ///< Menu flow document.
    script,      ///< Behavior source or module.
    texture,     ///< Image or texture resource.
    shader,      ///< Shader source or bytecode.
    material,    ///< Material document.
    video,       ///< Video resource.
    audio,       ///< Audio resource.
    localization ///< Localization data.
};

/// @brief Read the exact v1 serialized name of a recognized asset type.
[[nodiscard]] asset_type parse_type(std::string_view name);
/// @brief Obtain the exact v1 name; unknown enumeration values are rejected.
[[nodiscard]] std::string_view format_type(asset_type type);

/// @brief Authoring reference contains the stable ID and expected type only.
struct asset_ref
{
    asset_id id;                                        ///< Stable persistent identity.
    asset_type type{};                                  ///< Expected kind checked when resolving.
    auto operator<=>(const asset_ref&) const = default; ///< Exact typed identity comparison.
};

/// @brief Portable source location interpreted only by mounting infrastructure.
struct source_locator
{
    std::string root; ///< Logical key from the project's authorized root configuration.
    std::string path; ///< Normalized relative path using forward slashes.
    bool operator==(const source_locator&) const = default; ///< Exact authored spelling.
};

/// @brief Derived product binding is a named link to another typed record.
struct derived_product
{
    std::string role; ///< Unique product role for this source record.
    asset_ref target; ///< Persistent product identity.
    bool operator==(const derived_product&) const = default; ///< Exact authored mapping.
};

/// @brief Central authoring metadata for one asset generation.
struct asset_record
{
    asset_ref reference;                     ///< Persistent typed reference.
    source_locator source;                   ///< Project-relative source locator.
    std::uint64_t revision{1};               ///< Advance on changed content or import settings.
    std::uint64_t byte_size{};               ///< Exact logical extent of this source revision.
    std::string importer{"raw"};             ///< Stable importer vocabulary key.
    std::uint32_t importer_version{1};       ///< Importer implementation version.
    std::uint32_t settings_version{1};       ///< Version of the importer settings contract.
    std::vector<asset_ref> dependencies{};   ///< Required, typed dependency edges.
    std::vector<derived_product> products{}; ///< Named derived asset links.
    bool operator==(const asset_record&) const = default; ///< Exact metadata comparison.
};

/// @brief Editable central document; compile validates the entire candidate before publication.
struct catalog_document
{
    namespace_id space;          ///< Namespace of every owned record.
    std::uint64_t next_local{1}; ///< Monotonic allocation watermark, persisted through deletion.
    std::vector<asset_record> records{}; ///< Untrusted authored metadata before compilation.
    bool operator==(const catalog_document&) const = default; ///< Exact metadata comparison.
};

/// @brief Allocate a fresh identity and advance the watermark without adding a record.
[[nodiscard]] asset_id allocate_id(catalog_document& draft);

/// @brief Runtime resolution exposes revision and extent without exposing a physical locator.
struct asset_info
{
    asset_ref reference;       ///< Resolved typed persistent identity.
    std::uint64_t revision{};  ///< Authored content generation.
    std::uint64_t byte_size{}; ///< Logical length used for exact-range acquisition.
};

/// @brief Immutable, shared snapshot with allocation-free typed lookups.
class catalog
{
  public:
    /// @brief Validate bounds, locators, identities and graph integrity before creating a snapshot.
    [[nodiscard]] static catalog compile(catalog_document draft,
                                         const diagnostics::services& sinks = {});
    /// @brief Resolve a typed reference or report the missing ID or mismatched type.
    [[nodiscard]] asset_info resolve(asset_ref reference) const;
    /// @brief Borrow immutable sorted metadata for authoring, serialization or mount preparation.
    [[nodiscard]] const catalog_document& document() const noexcept;

  private:
    explicit catalog(std::shared_ptr<const catalog_document> snapshot); ///< Retain validated data.
    std::shared_ptr<const catalog_document> snapshot_; ///< Immutable shared catalog storage.
};

/// @brief Borrow acquired bytes and a separate source name for parser diagnostics.
struct xml_source
{
    std::string_view bytes;                 ///< Complete versioned UTF-8 XML document.
    std::string_view name{"asset-catalog"}; ///< Caller-supplied diagnostic identity.
};

/// @brief Parse strict versioned XML from bytes acquired by the composition root.
[[nodiscard]] catalog read_xml(xml_source source, const diagnostics::services& sinks = {});
/// @brief Produce deterministic canonical v1 XML; persistence is owned by the caller.
[[nodiscard]] std::string write_xml(const catalog& snapshot,
                                    const diagnostics::services& sinks = {});
} // namespace atlas::assets
