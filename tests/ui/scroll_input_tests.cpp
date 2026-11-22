/** @file @brief Focus reveal and frame-boundary scrolling through the reusable input session. */
#include "scroll_test_support.hpp"

namespace atlas::ui::tests
{
/// @brief Focusing the last item reveals its trailing boundary through ordinary queued commands.
TEST_F(scroll_fixture, FocusRevealsLastItemAtThePublicationBoundary)
{
    tree.set_panel(content, vertical_layout{});
    resize_content({});
    std::array<element_handle, 3> items;
    for (auto& item : items)
    {
        item = rectangle(content);
        auto style = tree.layout(item);
        style.intrinsic = {100, 40};
        tree.set_layout(item, style);
        input.bind(item, handler, {.focusable = true});
    }

    begin();
    input.focus(items.back());
    EXPECT_EQ(std::get<scroll_layout>(tree.panel(viewport)).offset.y, 0);
    input.process({});
    finish();
    EXPECT_EQ(std::get<scroll_layout>(tree.panel(viewport)).offset.y, 40);
}
} // namespace atlas::ui::tests
