/** @file @brief Construct native-free UI state from validated authored descriptors. */
#include "tree_build.hpp"

namespace atlas::ui_content::detail
{
namespace
{
/// @brief Translate authored optional dimensions into the existing absolute command vocabulary.
void initial_properties(ui::command_buffer& commands, ui::element_handle target,
                        const ui::requested_properties& values)
{
    constexpr std::uint64_t bootstrap_producer = 1;
    if (values.width_fixed)
    {
        commands.set(target, ui::property::width, values.width, bootstrap_producer);
    }

    if (values.height_fixed)
    {
        commands.set(target, ui::property::height, values.height, bootstrap_producer);
    }

    if (values.opacity != 1.f)
    {
        commands.set(target, ui::property::opacity, values.opacity, bootstrap_producer);
    }

    if (!values.visible)
    {
        commands.set(target, ui::property::visible, values.visible, bootstrap_producer);
    }
}

/// @brief Reuse native UI invariant checks for typed panel, paint, and parent-owned slots.
void metadata(ui::element_tree& tree, ui::element_handle target, const element& node)
{
    tree.set_layout(target, node.layout);
    tree.set_panel(target, node.panel);
    tree.set_visual(target, node.visual);
    tree.set_paint(target, node.paint);
    if (node.canvas)
    {
        tree.set_canvas_slot(target, *node.canvas);
    }

    if (node.stack)
    {
        tree.set_stack_slot(target, *node.stack);
    }
}

/// @brief Resolve an authored sibling ID into a checked handle for the new tree only.
std::optional<ui::boundary_relation> resolve_boundary(const elements& ids,
                                                      const std::optional<sibling_boundary>& source)
{
    if (!source)
    {
        return std::nullopt;
    }

    return ui::boundary_relation{ids.at(source->source), source->side, source->gap, source->sizing,
                                 source->overflow};
}
} // namespace

/// @brief The private tree can be discarded on any validation error.
elements build_tree(ui::element_tree& tree, const document& source)
{
    elements ids;
    tree.commands().begin_frame(1);
    for (const auto& node : source.elements)
    {
        try
        {
            const auto parent = node.parent.empty() ? ui::element_handle{} : ids.at(node.parent);
            const auto target = tree.create(parent);
            ids.emplace(node.id, target);
            metadata(tree, target, node);
            initial_properties(tree.commands(), target, node.requested);
        }
        catch (const std::logic_error& error)
        {
            throw std::invalid_argument("UI element " + node.id + ": " + error.what());
        }
    }

    tree.apply(tree.commands().seal());
    for (const auto& node : source.elements)
    {
        if (!node.horizontal && !node.vertical)
        {
            continue;
        }

        try
        {
            tree.set_boundaries(ids.at(node.id), {resolve_boundary(ids, node.horizontal),
                                                  resolve_boundary(ids, node.vertical)});
        }
        catch (const std::logic_error& error)
        {
            throw std::invalid_argument("UI boundary on " + node.id + ": " + error.what());
        }
    }

    return ids;
}
} // namespace atlas::ui_content::detail
