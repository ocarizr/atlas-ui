/** @file @brief Validate stable IDs, hierarchy, and document-local references. */
#include "document_validation.hpp"
#include <map>
#include <set>

namespace atlas::ui_content::detail
{
namespace
{
/// @brief Keep local names portable, bounded and distinct from native paths.
bool valid_name(std::string_view name)
{
    if (name.empty() || name.size() > 64)
    {
        return false;
    }

    for (const char ch : name)
    {
        if ((ch < 'a' || ch > 'z') && (ch < 'A' || ch > 'Z') && (ch < '0' || ch > '9') &&
            ch != '_' && ch != '-')
        {
            return false;
        }
    }

    return true;
}

/// @brief Reject invalid authored identities without requiring a mounted catalog.
void require_type(assets::asset_ref reference, assets::asset_type expected)
{
    if (reference.type != expected)
    {
        throw std::invalid_argument("Invalid UI asset reference type");
    }

    (void)assets::format_id(reference.id);
}

using model_names = std::set<std::string_view>; ///< Borrowed document-local context names.

/// @brief Validate independent descriptor names and storage limits before construction.
void validate_bindings(const element& node, const model_names& models)
{
    if (node.properties.size() > 4 || node.actions.size() > 64)
    {
        throw std::invalid_argument("UI element descriptor limit exceeded");
    }

    std::set<ui::property> targets;
    for (const auto& binding : node.properties)
    {
        if (!models.contains(binding.model) || binding.binding.source.empty() ||
            binding.binding.target > ui::property::visible ||
            binding.binding.mode > ui_mvvm::binding_mode::two_way ||
            !targets.insert(binding.binding.target).second)
        {
            throw std::invalid_argument("Invalid or duplicate UI property binding");
        }
    }

    for (const auto& route : node.actions)
    {
        const auto& action = route.binding;
        if (!models.contains(route.model) || action.action.empty() ||
            action.arguments.size() > 32 || action.event > ui::event_kind::ensure_visible ||
            action.route > ui::route_phase::bubble ||
            action.input_phase > input::action_phase::cancelled ||
            ((action.event == ui::event_kind::action) != (action.input_action != 0)))
        {
            throw std::invalid_argument("Invalid UI action binding");
        }
    }

    if (node.script)
    {
        require_type(node.script->source, assets::asset_type::script);
        if (!models.contains(node.script->model))
        {
            throw std::invalid_argument("Unknown UI behavior model: " + node.script->model);
        }
    }
}

/// @brief Track parent depth and the source element while checking the bounded tree.
struct location
{
    const element* source; ///< Stable position in the input document.
    std::size_t depth;     ///< Validated ancestry length.
};

using locations = std::map<std::string_view, location>; ///< Stable identity index.

/// @brief Confirm that an authored edge names another child in the same layout domain.
void validate_sibling(const element& node, const std::optional<sibling_boundary>& edge,
                      const locations& seen)
{
    if (!edge)
    {
        return;
    }

    const auto sibling = seen.find(edge->source);
    if (node.parent.empty() || edge->source == node.id || sibling == seen.end() ||
        sibling->second.source->parent != node.parent)
    {
        throw std::invalid_argument("Invalid UI sibling boundary: " + node.id + " -> " +
                                    edge->source);
    }
}

/// @brief Validate identity and parent order before creating any runtime handles.
void add_element(const element& node, locations& seen, const model_names& models)
{
    const auto parent = seen.find(node.parent);
    if (!valid_name(node.id) || (seen.empty() ? !node.parent.empty() : parent == seen.end()))
    {
        throw std::invalid_argument("Invalid UI element ID or parent: " + node.id + " -> " +
                                    node.parent);
    }

    const auto depth = parent == seen.end() ? std::size_t{1} : parent->second.depth + 1;
    if (depth > max_depth || !seen.emplace(node.id, location{&node, depth}).second)
    {
        throw std::invalid_argument("Duplicate UI element ID or depth limit exceeded: " + node.id);
    }

    if (node.paint.texture || node.paint.image ||
        (!node.requested.width_fixed && node.requested.width != 0) ||
        (!node.requested.height_fixed && node.requested.height != 0))
    {
        throw std::invalid_argument("Non-authoring UI element state: " + node.id);
    }

    if (node.texture)
    {
        require_type(*node.texture, assets::asset_type::texture);
    }

    validate_bindings(node, models);
}
} // namespace

/// @brief Validate all document-local names before resolving any external assets.
void validate(const document& source)
{
    if (source.elements.empty() || source.elements.size() > max_elements ||
        source.models.size() > max_models)
    {
        throw std::invalid_argument("UI document element or model count outside its limit");
    }

    model_names models;
    for (const auto& model : source.models)
    {
        require_type(model.source, assets::asset_type::view_model);
        if (!valid_name(model.name) || !models.insert(model.name).second)
        {
            throw std::invalid_argument("Invalid or duplicate UI model context: " + model.name);
        }
    }

    locations seen;
    for (const auto& node : source.elements)
    {
        try
        {
            add_element(node, seen, models);
        }
        catch (const std::invalid_argument& error)
        {
            throw std::invalid_argument("UI element " + node.id + ": " + error.what());
        }
    }

    for (const auto& node : source.elements)
    {
        validate_sibling(node, node.horizontal, seen);
        validate_sibling(node, node.vertical, seen);
    }
}
} // namespace atlas::ui_content::detail
