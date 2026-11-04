/** @file @brief Small deterministic panel forests reused by subtree scheduling facts. */
#pragma once
#include "layout_test_support.hpp"
#include <atlas/ui/element_tree.hpp>

namespace atlas::ui::tests
{
struct subtree_shape
{
    std::size_t panels = 8;  ///< Sibling panels per canvas.
    std::size_t leaves = 16; ///< Leaf children per panel.
}; ///< Named shape avoids ambiguous positional counts.

/// @brief Return the canvas identity; all panels/leaves retain ordinary owner-tree identity.
inline element_handle append_subtree_scenario(element_tree& tree, subtree_shape shape = {})
{
    const auto root = tree.create();
    tree.set_panel(root, canvas_layout{});
    for (std::size_t column = 0; column < shape.panels; ++column)
    {
        const auto panel = tree.create(root);
        tree.set_panel(panel, vertical_layout{2, stack_distribution::space_evenly});
        for (std::size_t index = 0; index < shape.leaves; ++index)
        {
            const auto leaf = tree.create(panel);
            layout_style style;
            style.intrinsic = {static_cast<float>(index % 13), static_cast<float>(column % 7)};
            tree.set_layout(leaf, style);
        }
    }

    return root;
}
} // namespace atlas::ui::tests
