#include "drag_content.hpp"
/** @file @brief Validate stable IDs, hierarchy, and document-local references. */
#include "document_validation.hpp"
#include "navigation_content.hpp"
#include <atlas/ui/palette.hpp>
#include <cmath>
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
    for (const auto key : std::array<std::string_view, 2>{node.color_key, node.material_color_key})
    {
        if (!key.empty() && !ui::valid_palette_key(key))
        {
            throw std::invalid_argument("Invalid semantic color key");
        }
    }

    if (!node.material_color_key.empty() && !node.material && !node.paint.material)
    {
        throw std::invalid_argument("Material color key requires a material");
    }

    if (node.properties.size() > 4 || node.actions.size() > 64 || node.events.size() > 64)
    {
        throw std::invalid_argument("UI element descriptor limit exceeded");
    }

    std::set<ui::property> targets;
    for (const auto& event : node.events)
    {
        if (event.event > authored_event::focus_exit || event.action.empty() ||
            event.action.size() > 128 || event.arguments.size() > 32 ||
            (!event.screen.empty() && !valid_name(event.screen)) ||
            (!event.route.empty() && !valid_name(event.route)))
        {
            throw std::invalid_argument("Invalid UI event/action binding");
        }

        mvvm::validate_text(event.action, 128);
        mvvm::validate_text(event.context, 64);
        if (!event.region.empty() && !valid_name(event.region))
        {
            throw std::invalid_argument("Invalid event submenu region");
        }

        for (const auto& value : event.arguments)
        {
            if (const auto* text = std::get_if<std::string>(&value))
            {
                mvvm::validate_text(*text, 4096);
            }

            if (const auto* number = std::get_if<double>(&value); number && !std::isfinite(*number))
            {
                throw std::invalid_argument("Nonfinite UI event argument");
            }
        }
    }

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
            action.arguments.size() > 32 || action.event > ui::event_kind::drag_cancelled ||
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

/// @brief Chrome and viewport must be separate branches of the same authored UI document.
void validate_scrollbar(const element& node, const locations& seen)
{
    if (!node.scrollbar)
    {
        return;
    }

    const auto& bar = *node.scrollbar;
    const auto owner = seen.find(bar.viewport);
    if (owner == seen.end() ||
        !std::holds_alternative<ui::scroll_layout>(owner->second.source->panel))
    {
        throw std::invalid_argument("Unknown scroll viewport on scrollbar: " + node.id);
    }

    for (auto current = seen.at(node.id).source; current;)
    {
        if (current->id == bar.viewport)
        {
            throw std::invalid_argument("Scrollbar must be outside its viewport subtree");
        }

        current = current->parent.empty() ? nullptr : seen.at(current->parent).source;
    }

    for (auto current = owner->second.source; current;)
    {
        if (current->id == node.id)
        {
            throw std::invalid_argument("Scrollbar cannot contain its viewport");
        }

        current = current->parent.empty() ? nullptr : seen.at(current->parent).source;
    }

    (void)ui::resolve_scrollbar({}, {},
                                {{}, bar.axis, bar.minimum_thumb, bar.hide_when_zero, bar.color});
}

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
    mvvm::validate_text(node.caption, 256);
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
        (node.paint.material && node.paint.material->shader) ||
        (!node.requested.width_fixed && node.requested.width != 0) ||
        (!node.requested.height_fixed && node.requested.height != 0))
    {
        throw std::invalid_argument("Non-authoring UI element state: " + node.id);
    }

    if (node.texture)
    {
        require_type(*node.texture, assets::asset_type::texture);
    }

    if (node.material)
    {
        require_type(*node.material, assets::asset_type::material);
        if (node.paint.material || node.texture)
        {
            throw std::invalid_argument(
                "Material assets own their shader values and texture pairing");
        }
    }

    validate_bindings(node, models);
}
} // namespace

/// @brief Validate all document-local names before resolving any external assets.
void validate(const document& source)
{
    validate_presentation(source.presentation);
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
        validate_drag(node);
        if (node.accessible)
        {
            ui::validate_accessibility(*node.accessible);
            if (node.decorative)
            {
                throw std::invalid_argument(
                    "decorative UI element cannot declare accessible semantics");
            }

            if ((node.accessible->invoke_action || node.accessible->select_action) &&
                !node.interaction && node.events.empty() && node.actions.empty() && !node.script)
            {
                throw std::invalid_argument("accessible action requires a semantic input handler");
            }
        }

        validate_sibling(node, node.horizontal, seen);
        validate_sibling(node, node.vertical, seen);
        validate_scrollbar(node, seen);
    }

    validate_grids(source);
    validate_navigation(source);
}
} // namespace atlas::ui_content::detail
