/**
 * @file
 * @brief Versioned viewport UI authoring data and its XML boundary.
 */
#pragma once
#include <atlas/assets/catalog.hpp>
#include <atlas/ui_mvvm/action_handler.hpp>
#include <atlas/ui_mvvm/binding_session.hpp>

namespace atlas::ui_content
{
/// @brief One document-local model instance backed by a typed persistent asset.
struct model_context
{
    std::string name;         ///< Stable local model name.
    assets::asset_ref source; ///< ViewModel definition asset.
};

/// @brief Named model field connection to the owning element.
struct property_binding
{
    std::string model;                   ///< Document-local context.
    ui_mvvm::binding_definition binding; ///< Typed property and direction.
};

/// @brief Named model action connection to the owning element.
struct action_binding
{
    std::string model;                          ///< Document-local context.
    ui_mvvm::action_binding_definition binding; ///< Trigger and typed constants.
};

/// @brief Isolated handler table in the shared UI Lua domain.
struct behavior
{
    assets::asset_ref source; ///< Persistent script asset.
    std::string model;        ///< Document-local context exposed to the script.
};

/// @brief Authored edge relation resolved from a sibling ID during construction.
struct sibling_boundary
{
    std::string source;                                            ///< Stable sibling ID.
    ui::boundary_side side = ui::boundary_side::after;             ///< Source/target edge pairing.
    float gap = 0;                                                 ///< Logical separation.
    ui::boundary_sizing sizing = ui::boundary_sizing::desired;     ///< Axis allocation.
    ui::boundary_overflow overflow = ui::boundary_overflow::allow; ///< Incompatible bounds policy.
};

/// @brief Serializable element data without any live handles or registry identities.
struct element
{
    std::string id;     ///< Unique stable ID in this document.
    std::string parent; ///< Empty for the single root; otherwise an earlier element.
    ui::requested_properties requested{}; ///< Initial scalar properties.
    ui::layout_style layout{};            ///< Geometry and alignment.
    ui::panel_layout panel{};             ///< Container policy.
    ui::visual_style visual{};            ///< General transform origin.
    ui::paint_style paint{}; ///< Appearance; transient texture key and sampling must be default.
    std::optional<assets::asset_ref> texture;           ///< Persistent image identity.
    std::optional<ui::canvas_slot> canvas;              ///< Canvas-owned child metadata.
    std::optional<ui::stack_slot> stack;                ///< Stack-owned child metadata.
    std::optional<sibling_boundary> horizontal;         ///< Horizontal sibling constraint.
    std::optional<sibling_boundary> vertical;           ///< Vertical sibling constraint.
    std::optional<ui::interaction_options> interaction; ///< Explicit routing policy.
    std::vector<property_binding> properties;           ///< Ordered declarations.
    std::vector<action_binding> actions;                ///< Ordered action routes.
    std::optional<behavior> script; ///< At most one behavior instance per element.
    std::string caption{};          ///< Optional authored text for a host's label presentation.
    std::optional<assets::asset_ref> material{}; ///< Persistent material template or override.
};

/// @brief Popup defaults shared by source assets and optional Flow-instance overrides.
struct popup_policy
{
    bool modal = true; ///< Suspend lower input; modeless pointer routing belongs to the host.
    bool dismiss_outside = false; ///< Outside dismissal must consume the gesture.
    bool dismiss_back = true;     ///< Permit the semantic Back action to close this popup.
    bool operator==(const popup_policy&) const = default; ///< Exact policy equality.
};

/// @brief Authored presentation type independent of transient views or graph node identities.
struct presentation_options
{
    assets::asset_type type = assets::asset_type::ui; ///< Legacy UI remains valid and unchanged.
    popup_policy popup{}; ///< Meaningful only for explicit popup assets.
    bool operator==(const presentation_options&) const = default; ///< Source/prepared equivalence.
};

/// @brief Validate the UI family, popup-only options and an optional catalog type contract.
void validate_presentation(presentation_options options,
                           std::optional<assets::asset_type> expected = {});

/// @brief Ordered authoring data shared by parsed XML and prepared/native sources.
struct document
{
    std::vector<model_context> models;   ///< Named ViewModel instances.
    std::vector<element> elements;       ///< Parent-before-child order preserves sibling order.
    presentation_options presentation{}; ///< Stable authored kind and popup defaults.
};

/// @brief Borrowed XML payload and diagnostic identity.
struct xml_source
{
    std::string_view bytes;                ///< UTF-8 source from the storage boundary.
    std::string_view name = "UI document"; ///< Diagnostic label.
};

/// @brief Parse bounded, strict XML and validate document-local names and relationships.
[[nodiscard]] document parse_xml(xml_source source, const diagnostics::services& sinks = {});
/// @brief Write canonical XML to bytes for the host's persistence boundary.
[[nodiscard]] std::string write_xml(const document& source,
                                    const diagnostics::services& sinks = {});
} // namespace atlas::ui_content
