/** @file @brief Scrollbar track paging, capture lifetime and deterministic drag cleanup. */
#include "scroll_interaction_support.hpp"

namespace atlas::ui::tests
{
/// @brief The same derived thumb geometry drives drawing and proportional pointer input.
TEST_F(scroll_navigation_fixture, ThumbDragUsesCaptureAndClampsBeyondTheTrack)
{
    list();
    const auto bar = chrome();
    open();
    input.process(std::array{input_event{input_kind::pointer_down, {95, 5}},
                             input_event{input_kind::pointer_move, {95, 29}}});
    EXPECT_EQ(input.captured(), bar);
    finish();
    EXPECT_EQ(position(), 60);
    open();
    replay({{input_kind::pointer_up, {95, 1000}}});
    EXPECT_EQ(input.captured(), element_handle{});
    EXPECT_EQ(position(), 120);
}

/// @brief Track clicks page by one viewport without capturing the pointer.
TEST_F(scroll_navigation_fixture, TrackClickPagesAndWheelOverChromeControlsItsViewport)
{
    list();
    chrome();
    open();
    replay({{input_kind::pointer_down, {95, 70}}});
    EXPECT_EQ(position(), 80);
    EXPECT_EQ(input.captured(), element_handle{});
    auto packet = wheel(-1);
    packet.position = {95, 40};
    open();
    replay({packet});
    EXPECT_EQ(position(), 40);
}

/// @brief A changed range cancels capture before any further raw drag callback.
TEST_F(scroll_navigation_fixture, ContentShrinkCancelsDragAndReclampsAtTheNextBoundary)
{
    list();
    const auto bar = chrome();
    open();
    replay({{input_kind::pointer_down, {95, 5}}});
    EXPECT_EQ(input.captured(), bar);
    for (unsigned index = 1; index < items.size(); ++index)
    {
        tree.remove(items[index]);
    }

    open();
    EXPECT_EQ(input.captured(), element_handle{});
    replay({{input_kind::pointer_move, {95, 70}}});
    EXPECT_EQ(position(), 0);
}

/// @brief Explicit pointer cancellation releases drag without requiring a button-up packet.
TEST_F(scroll_navigation_fixture, PointerCancellationReleasesCapture)
{
    list();
    chrome();
    open();
    replay({{input_kind::pointer_down, {95, 5}}, {input_kind::pointer_cancel}});
    EXPECT_EQ(input.captured(), element_handle{});
    EXPECT_EQ(position(), 0);
}
} // namespace atlas::ui::tests
