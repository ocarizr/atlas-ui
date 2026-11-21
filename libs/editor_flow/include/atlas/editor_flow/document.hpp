/**
 * @file
 * @brief Editor-owned incomplete Flow graphs and separate runtime compilation.
 */
#pragma once
#include <atlas/ui_content/flow.hpp>

namespace atlas::editor_flow
{
/// @brief Identities belong to separate stable namespaces, independent of display labels.
enum class item_kind : std::uint8_t
{
    none,      ///< No selected item.
    region,    ///< Runtime region identity.
    screen,    ///< Runtime screen/node identity.
    transition ///< Runtime route/edge identity.
};

/// @brief Stable graph item identity, never a vector offset or displayed name.
struct item_ref
{
    item_kind kind = item_kind::none;                 ///< Explicit identity namespace.
    std::string id{};                                 ///< Empty only for no selection.
    bool operator==(const item_ref&) const = default; ///< Compare the complete identity.
};

/// @brief Graph canvas metadata remains outside runtime/prepared navigation data.
struct item_layout
{
    item_ref item;     ///< Existing region, screen or route.
    std::string label; ///< Editable presentation, independent of item identity.
    double x = 0;      ///< Finite graph-space horizontal position.
    double y = 0;      ///< Finite graph-space vertical position.
};

/// @brief Persistent predicate IDs and scalar arguments, never Lua/domain lifetime tokens.
struct authored_condition
{
    std::string callable{}; ///< Empty for an AND group; namespaced predicate ID for a leaf.
    std::string context{};  ///< Named model context for a predicate leaf.
    std::vector<mvvm::value> arguments{};       ///< Exact scalar inputs.
    std::vector<authored_condition> children{}; ///< Ordered short-circuit AND members.
    bool negate = false; ///< Invert only a successfully evaluated value, never an error.
};

/// @brief Predicate decorators belong to the outgoing route, independent of UI controls.
struct transition_conditions
{
    std::string transition;        ///< Stable route identity.
    authored_condition expression; ///< Bounded AND/NOT tree.
};

/// @brief A structurally sound draft can contain disconnected nodes and unresolved references.
struct document
{
    std::string id; ///< Stable document identity supplied by project asset authoring.
    ui_content::flow_document graph; ///< Editable runtime declarations, possibly incomplete.
    std::vector<item_layout> layout; ///< Bounded editor-only positions/captions.
    item_ref selection{};            ///< Existing graph item, or no selection.
    std::vector<transition_conditions> conditions{}; ///< Version 2 outgoing predicate decorators.
};

/// @brief Check draft shape, identities and budgets without requiring a runnable graph.
void validate(const document& source);
/// @brief Read strict version 1 authoring XML; incomplete graph references remain authored.
[[nodiscard]] document read_xml(ui_content::xml_source source);
/// @brief Emit canonical version 1 authoring XML without filesystem access.
[[nodiscard]] std::string write_xml(const document& source);
/// @brief Explicitly import strict runtime Flow XML while preserving every persistent identity.
[[nodiscard]] document import_runtime(std::string id, ui_content::xml_source source);
} // namespace atlas::editor_flow
