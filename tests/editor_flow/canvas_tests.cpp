/** @file @brief Real Atlas pointer routing for the reusable graph surface. */
#include "fixture.hpp"
#include <atlas/editor_flow/canvas.hpp>

namespace atlas::editor_flow::tests
{
/// @brief Input/tree snapshots retain the injected allocator until the complete surface retires.
TEST(flow_canvas, RetainsAndReleasesHostMemoryResource)
{
    session owner{example()};
    workspace tool{owner};
    auto memory = std::make_shared<std::pmr::synchronized_pool_resource>();
    const std::weak_ptr<std::pmr::memory_resource> borrowed = memory;
    {
        canvas view{tool, {}, memory};
        memory.reset();
        EXPECT_FALSE(borrowed.expired());
    }
    EXPECT_TRUE(borrowed.expired());
}

/// @brief Capture survives a repaint between motion and release without duplicating revisions.
TEST(flow_canvas, CapturedDragCommitsOneRevisionThroughAtlasInput)
{
    session owner{example()};
    workspace tool{owner};
    jobs::job_system jobs{1};
    ui::layout_engine layout{jobs.dispatcher()};
    ui::paint_engine paint{jobs.dispatcher()};
    canvas view{tool};
    ASSERT_FALSE(view.arrange({0, 0, 600, 400}, layout, paint).quads().empty());
    const auto revision = owner.revision();
    const std::array events{ui::input_event{ui::input_kind::pointer_down, {30, 35}},
                            ui::input_event{ui::input_kind::pointer_move, {80, 75}}};
    EXPECT_TRUE(view.process(events));
    EXPECT_EQ(owner.revision(), revision);
    (void)view.arrange({0, 0, 600, 400}, layout, paint);
    EXPECT_TRUE(view.process(std::array{ui::input_event{ui::input_kind::pointer_up, {80, 75}}}));
    EXPECT_DOUBLE_EQ(owner.current().layout.front().x, 60);
    EXPECT_DOUBLE_EQ(owner.current().layout.front().y, 60);
    EXPECT_TRUE(owner.undo());
    EXPECT_EQ(owner.revision(), revision);
}

/// @brief Cancel and out-of-viewport input leave every persisted node unchanged.
TEST(flow_canvas, CancelAndOutsidePressDoNotMoveNodes)
{
    session owner{example()};
    workspace tool{owner};
    jobs::job_system jobs{1};
    ui::layout_engine layout{jobs.dispatcher()};
    ui::paint_engine paint{jobs.dispatcher()};
    canvas view{tool};
    (void)view.arrange({0, 0, 600, 400}, layout, paint);
    const std::array events{ui::input_event{ui::input_kind::pointer_down, {30, 35}},
                            ui::input_event{ui::input_kind::pointer_move, {80, 75}},
                            ui::input_event{ui::input_kind::pointer_cancel}};
    (void)view.process(events);
    EXPECT_FALSE(owner.dirty());
    EXPECT_DOUBLE_EQ(tool.position({item_kind::screen, "home"}).x, 10);
    (void)view.process(std::array{ui::input_event{ui::input_kind::pointer_down, {700, 500}}});
    EXPECT_FALSE(owner.dirty());
}
} // namespace atlas::editor_flow::tests
