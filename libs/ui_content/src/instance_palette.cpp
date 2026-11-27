/** @file @brief Sparse semantic tint dependencies resolved once and applied on the UI owner. */
#include "instance_state.hpp"

namespace atlas::ui_content
{
/// @brief Resolve every binding before mutating the private candidate's initial appearance.
void instance::impl::bind_palette(const document& source,
                                  std::shared_ptr<ui::palette_context> context)
{
    palette = std::move(context);
    const auto selected = palette ? palette->snapshot() : nullptr;
    for (const auto& node : source.elements)
    {
        if (node.color_key.empty() && node.material_color_key.empty())
        {
            continue;
        }

        if (!selected)
        {
            throw std::invalid_argument("Semantic colors require a palette context");
        }

        palette_binding binding{elements.at(node.id), {}, {}};
        if (!node.color_key.empty())
        {
            binding.paint = selected->find(node.color_key);
        }

        if (!node.material_color_key.empty())
        {
            if (!tree.material(binding.target))
            {
                throw std::invalid_argument("Material palette color requires a material");
            }

            binding.material = selected->find(node.material_color_key);
        }

        palette_bindings.push_back(binding);
    }

    synchronize_palette();
}

/// @brief A theme changes the underlying tint, while active animation channels keep their output.
bool instance::impl::palette_base(ui::element_handle target, ui::property field, float value)
{
    bool owned = false;
    const auto update = [&](auto& tracks, bool active)
    {
        for (auto& track : tracks)
        {
            if (track.target == target && track.property == field)
            {
                track.base = value;
                owned |= active;
            }
        }
    };
    if (animation)
    {
        update(animation->tracks, true);
    }

    for (auto& active : players)
    {
        update(active.clip.tracks, active.started);
    }

    for (auto& [name, group] : animation_states)
    {
        (void)name;
        update(group.channels, group.selected.has_value());
    }

    return owned;
}

/// @brief Compare only bound colors; unchanged themes and unreferenced keys do no tree work.
void instance::impl::synchronize_palette()
{
    if (!palette)
    {
        return;
    }

    const auto selected = palette->snapshot();
    if (selected == palette_snapshot)
    {
        return;
    }

    const diagnostics::operation_scope scope{diagnostics, {"ui.palette.apply", "ui.paint"}};
    for (const auto& binding : palette_bindings)
    {
        if (!tree.contains(binding.target))
        {
            continue;
        }

        if (binding.paint && (!palette_snapshot || selected->color(*binding.paint) !=
                                                       palette_snapshot->color(*binding.paint)))
        {
            auto style = tree.paint(binding.target);
            style.color = selected->color(*binding.paint);
            tree.set_paint(binding.target, style);
        }

        if (binding.material &&
            (!palette_snapshot ||
             selected->color(*binding.material) != palette_snapshot->color(*binding.material)))
        {
            auto style = tree.material(binding.target);
            if (!style)
            {
                continue;
            }

            const auto& tint = selected->color(*binding.material);
            for (std::size_t i = 0; i < tint.size(); ++i)
            {
                const auto field = static_cast<ui::property>(
                    static_cast<unsigned>(ui::property::material_red) + i);
                if (!palette_base(binding.target, field, tint[i]))
                {
                    style->tint[i] = tint[i];
                }
            }

            tree.set_material(binding.target, *style);
        }
    }

    palette_snapshot = selected;
}
} // namespace atlas::ui_content
