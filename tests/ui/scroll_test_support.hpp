/** @file @brief Shared scroll geometry fixtures with isolated tree and worker ownership. */
#pragma once
#include "input_test_support.hpp"
#include "layout_test_support.hpp"

namespace atlas::ui::tests
{
/// @brief A single finite viewport and one freely sized content subtree per fact.
class scroll_fixture : public input_fixture
{
  protected:
    void SetUp() override
    {
        viewport = tree.create();
        tree.set_panel(viewport, scroll_layout{});
        content = rectangle(viewport);
        resize_content({100, 200});
        tree.set_viewport({0, 0, 100, 80});
    } ///< Reset ownership and numeric state for every test.

    void resize_content(layout_extent size)
    {
        auto style = tree.layout(content);
        style.intrinsic = size;
        tree.set_layout(content, style);
    } ///< A natural content change does not rewrite requested coordinates.

    void offset(layout_point value)
    {
        auto policy = std::get<scroll_layout>(tree.panel(viewport));
        policy.offset = value;
        tree.set_panel(viewport, policy);
    } ///< The frame owner commits programmatic offsets before capturing input.

    layout_snapshot calculate()
    {
        return layout.calculate(tree.snapshot());
    } ///< Use the same current viewport as paint and input.

    scroll_geometry scrolling(const layout_snapshot& result) const
    {
        const auto value = result.find_scroll(viewport);
        if (!value)
        {
            throw std::logic_error("expected scroll metadata");
        }

        return *value;
    } ///< Never dereference an unchecked optional in fixture assertions.

    element_handle viewport; ///< Stable root identity.
    element_handle content;  ///< Single content subtree.
};
} // namespace atlas::ui::tests
