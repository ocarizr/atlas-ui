/** @file @brief Renderer handoff packets retain immutable UI data without graphics ownership. */
#include <atlas/jobs/job_system.hpp>
#include <atlas/ui/element_tree.hpp>
#include <atlas/ui_renderer/paint_renderer.hpp>
#include <gtest/gtest.h>
#include <thread>

/// @brief Verify empty payload is an immutable render host packet.
TEST(UiFramePayload, EmptyPayloadIsAnImmutableRenderHostPacket)
{
    const auto payload =
        std::make_shared<const atlas::ui_renderer::frame_payload>(atlas::ui::paint_snapshot{});
    atlas::render_host::frame_snapshot frame;
    frame.content = payload;
    EXPECT_TRUE(payload->paint.quads().empty());
    EXPECT_EQ(frame.content.get(), payload.get());
}

/// @brief CPU packet lifetime is independent from authoring state, workers and handoff thread.
TEST(UiFramePayload, RetainsGeometryAfterAuthoringAndWorkerShutdown)
{
    atlas::render_host::frame_snapshot frame;
    frame.content = []
    {
        atlas::jobs::job_system workers{1};
        atlas::ui::element_tree tree;
        const auto root = tree.create();
        atlas::ui::paint_style style;
        style.fill = true;
        tree.set_paint(root, style);
        tree.set_viewport({0, 0, 100, 80});
        atlas::ui::layout_engine layout{workers.dispatcher()};
        atlas::ui::paint_engine paint{workers.dispatcher()};
        const auto source = tree.snapshot();
        return std::make_shared<const atlas::ui_renderer::frame_payload>(
            paint.calculate(source, layout.calculate(source)));
    }();
    std::jthread consumer{
        [packet = std::move(frame.content)]
        {
            const auto* ui = dynamic_cast<const atlas::ui_renderer::frame_payload*>(packet.get());
            ASSERT_NE(ui, nullptr);
            ASSERT_EQ(ui->paint.quads().size(), 1);
            EXPECT_EQ(ui->paint.quads()[0].bounds, (atlas::ui::layout_rect{0, 0, 100, 80}));
        }};
}
