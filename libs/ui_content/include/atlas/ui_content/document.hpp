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

/// @brief Semantic authoring events independent of pointer, keyboard or controller activation.
enum class authored_event : std::uint8_t
{
    click,       ///< Successful press/release inside, or performed semantic activation.
    focus_enter, ///< Focus acquired, distinct from hover.
    focus_exit   ///< Focus lost.
};

/// @brief Stable event/action data owned by the UI asset, resolved by its Flow host.
struct event_action
{
    authored_event event = authored_event::click; ///< Supported semantic control event.
    std::string action;    ///< Built-in atlas action or registered native/Lua descriptor ID.
    std::string context{}; ///< Named ViewModel context for a registered action.
    std::string screen{};  ///< Explicit Flow screen context; reusable widgets require host binding.
    std::string route{};   ///< Exact permitted route ID, never a path or display label.
    std::vector<mvvm::value> arguments{}; ///< Exact typed scalar constants.
    std::uint32_t input_action = 0;       ///< Optional performed semantic activation identity.
    std::string region{}; ///< Optional explicit Tab Group/Submenu Area navigation scope.
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

/// @brief Authored chrome reference resolved to a typed viewport handle during construction.
struct scrollbar_link
{
    std::string viewport; ///< Document-local scroll-owner ID, including forward references.
    ui::scroll_axis axis = ui::scroll_axis::vertical; ///< Controlled axis.
    float minimum_thumb = 12;                         ///< Logical minimum thumb length.
    bool hide_when_zero = true;                       ///< Hide chrome when content fits.
    std::array<float, 4> color{0.7f, 0.7f, 0.7f, 1};  ///< Solid thumb color.
};

/// @brief Document-local focus destinations without transient runtime identities.
struct focus_navigation
{
    std::array<std::string, 6> destinations{}; ///< Direction-indexed stable element IDs.
    std::array<bool, 6> blocked{};             ///< Explicitly disabled directions.
    std::int32_t tab_order = 0;                ///< Ordered focus priority.
    bool tab_stop = true;                      ///< Ordered traversal participation.
};

/// @brief Optional view-level portable keyboard and semantic action configuration.
struct navigation_input
{
    ui::focus_navigation_actions actions{}; ///< IDs defined by the host's input contexts.
    bool keyboard = true;                   ///< Opt-in arrows and Tab/Shift+Tab.
};

/// @brief Serializable source policy; an empty type requests a dynamic Lua prepare offer.
struct drag_source_definition
{
    std::string type; ///< Stable payload type key.
    std::string text; ///< Owned UTF-8 text, not a file path.
    ui::drag_operations operations = ui::drag_operations::copy; ///< Copy/move capability.
    float threshold = 4; ///< Logical pointer travel before lifting.
};

/// @brief Serializable element data without live handles or registry identities.
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
    std::vector<event_action> events{};          ///< Editor-authored semantic action bindings.
    std::optional<scrollbar_link> scrollbar{};   ///< Sparse authored chrome behavior.
    std::optional<ui::grid_slot> grid{}; ///< Grid-owned explicit coordinates or automatic spans.
    std::optional<focus_navigation> focus{}; ///< Explicit focus relationships and ordered policy.
    std::optional<ui::focus_navigation_scope> focus_scope{}; ///< Search group and semantic routes.
    std::optional<drag_source_definition> drag_source{};     ///< Optional typed offer.
    std::optional<ui::drop_target_options> drop_target{};    ///< Accepted typed transfers.
    std::optional<ui::accessible_element> accessible{}; ///< Sparse semantic authoring override.
    bool decorative = false; ///< Omit default semantics without hiding meaningful descendants.
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
    std::optional<navigation_input> focus_input{}; ///< Device-independent navigation opt-in.
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
