/** @file @brief Validate complete packets before nonallocating requested-state publication. */
#include "boundary_validation.hpp"
#include "canvas_validation.hpp"
#include "tree_state.hpp"

namespace atlas::ui
{
namespace
{
/// @brief Decode only validated width/height commands; zero and automatic are distinct.
std::pair<float, bool> extent_value(const property_value& value)
{
    return std::holds_alternative<std::monostate>(value) ? std::pair{0.f, false}
                                                         : std::pair{std::get<float>(value), true};
}

/// @brief Identify the authored canvas fields that must validate together.
bool canvas_field(property field)
{
    return field >= property::canvas_x_offset_min;
}

/// @brief Select one scalar from a staged canvas slot.
float& canvas_value(canvas_slot& slot, property field)
{
    switch (field)
    {
        case property::canvas_x_offset_min:
            return slot.horizontal.offset_min;
        case property::canvas_x_offset_max:
            return slot.horizontal.offset_max;
        case property::canvas_y_offset_min:
            return slot.vertical.offset_min;
        case property::canvas_y_offset_max:
            return slot.vertical.offset_max;
        case property::canvas_x_anchor_min:
            return slot.horizontal.anchor_min;
        case property::canvas_x_anchor_max:
            return slot.horizontal.anchor_max;
        case property::canvas_y_anchor_min:
            return slot.vertical.anchor_min;
        case property::canvas_y_anchor_max:
            return slot.vertical.anchor_max;
        default:
            throw std::logic_error("UI command is not a canvas property");
    }
}

/// @brief Compare and update one validated absolute value.
bool assign(float& destination, float value)
{
    if (destination == value)
    {
        return false;
    }

    destination = value;
    return true;
}

/// @brief Select an edge in declaration order for the contiguous inset command ranges.
float& edge(layout_insets& values, property field, property first)
{
    const std::array edges{&layout_insets::left, &layout_insets::top, &layout_insets::right,
                           &layout_insets::bottom};
    return values.*edges[static_cast<unsigned>(field) - static_cast<unsigned>(first)];
}

/// @brief A stack spacing command is valid for either stack orientation.
float& spacing(panel_layout& panel)
{
    if (auto* horizontal = std::get_if<horizontal_layout>(&panel))
    {
        return horizontal->spacing;
    }

    if (auto* vertical = std::get_if<vertical_layout>(&panel))
    {
        return vertical->spacing;
    }

    throw std::invalid_argument("UI spacing command requires a stack");
}
} // namespace

/// @brief Resolve target domains and validate final slot values as a group.
bool element_tree::impl::prepare_writes(std::span<const property_write> writes,
                                        std::optional<canvas_writes>& canvas)
{
    bool geometry_changed = false;

    for (const auto& write : writes)
    {
        const auto index = require(write.target);
        auto& target_node = at(index);
        const auto& current = target_node.requested;
        if (write.field == property::width || write.field == property::height)
        {
            const auto [value, fixed] = extent_value(write.value);
            geometry_changed = geometry_changed ||
                               (write.field == property::width
                                    ? current.width != value || current.width_fixed != fixed
                                    : current.height != value || current.height_fixed != fixed);
        }
        else if (write.field >= property::margin_left && write.field <= property::margin_bottom)
        {
            geometry_changed |= edge(target_node.layout.margin, write.field,
                                     property::margin_left) != std::get<float>(write.value);
        }
        else if (write.field >= property::padding_left && write.field <= property::padding_bottom)
        {
            geometry_changed |= edge(target_node.layout.padding, write.field,
                                     property::padding_left) != std::get<float>(write.value);
        }
        else if (write.field == property::spacing)
        {
            geometry_changed |= spacing(target_node.panel) != std::get<float>(write.value);
        }
        else if (canvas_field(write.field))
        {
            if (!canvas)
            {
                canvas.emplace(memory.get());
            }

            const auto entry = canvas->try_emplace(index, child_slot(index)).first;
            canvas_value(entry->second, write.field) = std::get<float>(write.value);
        }
    }

    if (canvas)
    {
        std::erase_if(
            *canvas,
            [&](const auto& entry)
            {
                const auto& [index, slot] = entry;
                const auto parent = at(index).parent;
                const auto* panel =
                    parent == none ? nullptr : std::get_if<canvas_layout>(&at(parent).panel);
                if (!panel)
                {
                    throw std::invalid_argument("UI canvas commands require a canvas parent");
                }

                detail::validate_slot(slot, *panel);
                detail::validate_boundary_values(child_boundaries(index), slot);
                return child_slot(index) == slot;
            });
        geometry_changed |= !canvas->empty();
    }

    return geometry_changed;
}

/// @brief Allocate missing slots before committing any layout state.
void element_tree::impl::apply_canvas(const canvas_writes& canvas)
{
    // Reserve sparse components before publishing any requested property.
    std::pmr::vector<entt::entity> inserted{memory.get()};
    inserted.reserve(canvas.size());
    try
    {
        for (const auto& [index, slot] : canvas)
        {
            const auto entity = slots[index].entity;
            if (slot != canvas_slot{} && !registry.all_of<canvas_slot>(entity))
            {
                registry.emplace<canvas_slot>(entity);
                inserted.push_back(entity);
            }
        }
    }
    catch (...)
    {
        for (const auto entity : inserted)
        {
            registry.remove<canvas_slot>(entity);
        }

        throw;
    }

    for (const auto& [index, slot] : canvas)
    {
        const auto entity = slots[index].entity;
        if (slot == canvas_slot{})
        {
            registry.remove<canvas_slot>(entity);
        }
        else
        {
            *registry.try_get<canvas_slot>(entity) = slot;
        }

        subtree(index, true);
        ancestors(at(index).parent);
    }
}

/// @brief Commit scalar values using the ordinary tree invalidation rules.
void element_tree::impl::apply_scalar(const property_write& write)
{
    auto& target_node = at(write.target.index);
    auto& value = target_node.requested;
    bool changed = false;
    bool layout = false;
    switch (write.field)
    {
        case property::width:
        {
            const auto [extent, fixed] = extent_value(write.value);
            changed = value.width != extent || value.width_fixed != fixed;
            value.width = extent;
            value.width_fixed = fixed;
            layout = true;
            break;
        }
        case property::height:
        {
            const auto [extent, fixed] = extent_value(write.value);
            changed = value.height != extent || value.height_fixed != fixed;
            value.height = extent;
            value.height_fixed = fixed;
            layout = true;
            break;
        }
        case property::opacity:
            changed = value.opacity != std::get<float>(write.value);
            value.opacity = std::get<float>(write.value);
            break;
        case property::visible:
            changed = value.visible != std::get<bool>(write.value);
            value.visible = std::get<bool>(write.value);
            break;
        case property::translate_x:
            changed = assign(target_node.translation.x, std::get<float>(write.value));
            break;
        case property::translate_y:
            changed = assign(target_node.translation.y, std::get<float>(write.value));
            break;
        case property::scale_x:
            changed = assign(target_node.scale.x, std::get<float>(write.value));
            break;
        case property::scale_y:
            changed = assign(target_node.scale.y, std::get<float>(write.value));
            break;
        case property::rotate:
            changed = assign(target_node.rotation, std::get<float>(write.value));
            break;
        case property::margin_left:
        case property::margin_top:
        case property::margin_right:
        case property::margin_bottom:
            changed = assign(edge(target_node.layout.margin, write.field, property::margin_left),
                             std::get<float>(write.value));
            layout = true;
            break;
        case property::padding_left:
        case property::padding_top:
        case property::padding_right:
        case property::padding_bottom:
            changed = assign(edge(target_node.layout.padding, write.field, property::padding_left),
                             std::get<float>(write.value));
            layout = true;
            break;
        case property::spacing:
            changed = assign(spacing(target_node.panel), std::get<float>(write.value));
            layout = true;
            break;
        default:
            break; // Canvas fields were committed as a group.
    }

    if (changed)
    {
        subtree(write.target.index, layout);
        if (layout)
        {
            ancestors(target_node.parent);
        }
    }
}

/// @brief Immutable packets originate from the sole bound producer; validate freshness atomically.
void element_tree::apply(const command_batch& batch)
{
    impl_->check();
    const impl::guard guard{impl_->busy};
    const diagnostics::operation_scope scope{impl_->destinations, {"ui.tree.apply", "ui.cpu"}};
    if (batch.session() != impl_->session || batch.frame() < impl_->frame || batch.frame() == 0 ||
        (batch.frame() == impl_->frame ? batch.ordinal() != impl_->ordinal + 1
                                       : batch.ordinal() != 1))
    {
        throw std::invalid_argument("UI tree needs a session-bound batch in frame/ordinal order");
    }

    std::optional<impl::canvas_writes> canvas;
    const bool geometry_changed = impl_->prepare_writes(batch.writes(), canvas);
    if (geometry_changed)
    {
        impl_->ensure_layout_revision();
    }

    if (canvas && !canvas->empty())
    {
        impl_->apply_canvas(*canvas);
    }

    for (const auto& write : batch.writes())
    {
        impl_->apply_scalar(write);
    }

    impl_->frame = batch.frame();
    impl_->ordinal = batch.ordinal();
    if (geometry_changed)
    {
        ++impl_->layout_revision;
    }
}
} // namespace atlas::ui
