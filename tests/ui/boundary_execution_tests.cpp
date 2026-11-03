/** @file @brief Seeded acyclic models agree with an independent reference and shared-pool phases.
 */
#include "boundary_test_support.hpp"
#include <algorithm>
#include <random>

namespace atlas::ui::tests
{
/// @brief Reference input is small and independent from runtime capture, ordering and caches.
struct reference_axis
{
    float position; ///< Expected border start.
    float size;     ///< Expected border extent.
    float before;   ///< Leading outer margin.
    float after;    ///< Trailing outer margin.
};

/// @brief Direct interval arithmetic supplies a separate model, without production helpers.
reference_axis reference_boundary(reference_axis source, reference_axis target,
                                  const boundary_relation& relation)
{
    const auto edge = relation.side == boundary_side::after
                          ? source.position + source.size + source.after + relation.gap
                          : source.position - source.before - relation.gap;
    const auto available = relation.side == boundary_side::after ? 200 - edge : edge;
    if (relation.sizing == boundary_sizing::remaining)
    {
        target.size = std::max(0.f, available - target.before - target.after);
    }

    target.position = relation.side == boundary_side::after ? edge + target.before
                                                            : edge - target.after - target.size;
    return target;
}

/// @brief Reverse insertion deliberately differs from dependency order; axes use different edges.
TEST(boundary_execution, seeded_models_match_and_parallel_results_are_exact)
{
    jobs::job_system pool{4};
    layout_engine serial{pool.dispatcher(), {256, 1, 1}};
    layout_engine parallel{pool.dispatcher(), {256, 1, 4}};
    std::mt19937 random{0x113u};
    for (int iteration = 0; iteration < 32; ++iteration)
    {
        SCOPED_TRACE(iteration);
        element_tree tree{{256}};
        std::vector<std::pair<element_handle, layout_rect>> expected;
        for (int branch = 0; branch < 4; ++branch)
        {
            const auto root = tree.create();
            tree.set_panel(root, canvas_layout{});
            std::vector<element_handle> children(12);
            for (std::size_t index = children.size(); index != 0; --index)
            {
                children[index - 1] = tree.create(root);
            }

            std::vector<reference_axis> horizontal;
            std::vector<reference_axis> vertical;
            for (std::size_t index = 0; index < children.size(); ++index)
            {
                layout_style style;
                style.intrinsic = {static_cast<float>(random() % 20),
                                   static_cast<float>(random() % 30)};
                style.margin = {static_cast<float>(random() % 3), static_cast<float>(random() % 3),
                                static_cast<float>(random() % 3), static_cast<float>(random() % 3)};
                tree.set_layout(children[index], style);
                reference_axis x{style.margin.left, style.intrinsic.width, style.margin.left,
                                 style.margin.right};
                reference_axis y{style.margin.top, style.intrinsic.height, style.margin.top,
                                 style.margin.bottom};
                if (index != 0)
                {
                    const auto predecessor_x = static_cast<std::size_t>(random()) % index;
                    const auto predecessor_y = static_cast<std::size_t>(random()) % index;
                    const auto relation = [&](std::size_t predecessor)
                    {
                        return boundary_relation{children[predecessor],
                                                 (random() % 2) != 0 ? boundary_side::after
                                                                     : boundary_side::before,
                                                 static_cast<float>(random() % 4),
                                                 (random() % 2) != 0 ? boundary_sizing::desired
                                                                     : boundary_sizing::remaining};
                    };
                    const auto h = relation(predecessor_x);
                    const auto v = relation(predecessor_y);
                    tree.set_boundaries(children[index], {h, v});
                    x = reference_boundary(horizontal[predecessor_x], x, h);
                    y = reference_boundary(vertical[predecessor_y], y, v);
                }

                horizontal.push_back(x);
                vertical.push_back(y);
                expected.push_back({children[index], {x.position, y.position, x.size, y.size}});
            }
        }

        tree.set_viewport({0, 0, 200, 200});
        const auto input = tree.snapshot();
        const auto actual = parallel.calculate(input);
        const auto sequential = serial.calculate(input);
        ASSERT_EQ(actual.items().size(), sequential.items().size());
        EXPECT_TRUE(
            std::equal(actual.items().begin(), actual.items().end(), sequential.items().begin()));
        for (const auto& [target, bounds] : expected)
        {
            EXPECT_EQ(require_geometry(actual.find(target)).bounds, bounds);
        }

        EXPECT_EQ(serial.statistics().admitted_jobs, 0U);
        EXPECT_GT(parallel.statistics().admitted_jobs, 0U);
    }
}
} // namespace atlas::ui::tests
