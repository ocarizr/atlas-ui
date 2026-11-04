/** @file @brief Reproducible wide, nested, related and independent-root layout inputs. */
#include "layout_probe.hpp"

namespace atlas::ui::benchmarks
{
/// @brief Exact structural capacity avoids mistaking unused default reservation for useful data.
layout_probe::layout_probe(jobs::job_dispatcher dispatcher, layout_case scenario,
                           layout_limits policy, diagnostics::services destinations)
    : tree({scenario.roots * (1 + scenario.depth + scenario.panels * (1 + scenario.leaves))},
           memory, destinations),
      engine(std::move(dispatcher), policy, memory, std::move(destinations))
{
    unchanged = scenario.unchanged;
    roots = scenario.roots;
    max_chunks = policy.max_chunks;
    for (std::uint32_t branch = 0; branch < scenario.roots; ++branch)
    {
        auto root = tree.create();
        for (std::uint32_t depth = 0; depth < scenario.depth; ++depth)
        {
            root = tree.create(root);
        }

        tree.set_panel(root, canvas_layout{});
        element_handle previous;
        for (std::uint32_t column = 0; column < scenario.panels; ++column)
        {
            const auto panel = tree.create(root);
            tree.set_panel(panel, vertical_layout{2, stack_distribution::space_evenly});
            if (scenario.related && previous != element_handle{})
            {
                tree.set_boundaries(panel,
                                    {{}, boundary_relation{previous, boundary_side::after, 3}});
            }

            previous = panel;
            for (std::uint32_t index = 0; index < scenario.leaves; ++index)
            {
                const auto child = tree.create(panel);
                layout_style style;
                style.intrinsic = {static_cast<float>(1 + index % 13),
                                   static_cast<float>(1 + column % 7)};
                tree.set_layout(child, style);
                changed = child;
            }
        }

        changes.push_back(changed);
    }

    tree.set_viewport({0, 0, 1280, 720});
}
} // namespace atlas::ui::benchmarks
