/** @file @brief Bounded domain storage, retained payload leases and optional profiling. */
#include "drag_test_support.hpp"
#include "resource_test_support.hpp"
#include <sstream>

namespace atlas::ui::tests
{
/// @brief Payload containers release their PMR allocations with the final retained handle.
TEST(drag_payload_resource, RetainsAllocatorUntilLastPayloadAndThenReleasesAllBytes)
{
    auto memory = std::make_shared<failing_resource>();
    std::weak_ptr<failing_resource> lifetime = memory;
    auto value = drag_payload::text("test.item", std::string(1024, 'a'), memory);
    EXPECT_GE(memory->live_bytes, 1024);
    auto retained = value;
    value = {};
    memory.reset();
    EXPECT_FALSE(lifetime.expired());
    retained = {};
    EXPECT_TRUE(lifetime.expired());
}

/// @brief Admission reserves domain scratch; repeated pointer queries need no further PMR
/// allocation.
TEST_F(drag_fixture, DomainMotionReusesBoundedStorageAndCapturesOptionalScopes)
{
    const auto memory = std::make_shared<failing_resource>();
    const auto profiler = std::make_shared<diagnostics::profiler>();
    begin();
    drag.end_frame();
    {
        drag_drop_session observed{{}, memory, {{}, profiler}};
        const std::array views{&input, &destination};
        observed.begin_frame(1, views);
        EXPECT_FALSE(observed.process({input_kind::pointer_down, {10, 10}}));
        ASSERT_TRUE(observed.process({input_kind::pointer_move, {150, 20}}));
        const auto calls = memory->calls;
        memory->remaining = 0;
        for (int i = 0; i < 100; ++i)
        {
            EXPECT_TRUE(observed.process({input_kind::pointer_move, {150, 20}}));
        }

        EXPECT_EQ(memory->calls, calls);
        EXPECT_LT(memory->live_bytes, 4096); // Domain PMR only, not application or handler storage.
        observed.cancel();
        observed.end_frame();
    }
    EXPECT_EQ(memory->live_bytes, 0);
    std::ostringstream trace;
    profiler->write_chrome_trace(trace);
    for (const auto* name : {"ui.drag.dispatch", "ui.drag.callback"})
    {
        EXPECT_EQ(trace.str().find(name) != std::string::npos,
                  diagnostics::instrumentation_enabled);
    }

    const std::array views{&input, &destination};
    drag.begin_frame(2, views);
    finish();
}

/// @brief Exhausted policy work cancels ownership instead of leaving a captured partial transfer.
TEST_F(drag_fixture, QueryBudgetExhaustionReleasesCapture)
{
    begin();
    drag.end_frame();
    {
        drag_drop_session bounded{{.max_queries = 1}};
        const std::array views{&input, &destination};
        bounded.begin_frame(1, views);
        EXPECT_THROW((void)bounded.lift(input, source), std::overflow_error);
        EXPECT_EQ(input.captured(), element_handle{});
        EXPECT_EQ(bounded.feedback().phase, drag_phase::idle);
        bounded.end_frame();
    }
    const std::array views{&input, &destination};
    drag.begin_frame(2, views);
    finish();
}

/// @brief Removed generations release configured payload memory on the next input boundary.
TEST_F(drag_fixture, RemovedSourceDoesNotRetainItsPayloadResource)
{
    auto memory = std::make_shared<failing_resource>();
    std::weak_ptr<failing_resource> weak = memory;
    input.configure_drag_source(source,
                                {drag_payload::text("test.item", std::string(1024, 'x'), memory)});
    memory.reset();
    ASSERT_FALSE(weak.expired());
    tree.remove(source);
    begin();
    EXPECT_TRUE(weak.expired());
    finish();
}

/// @brief Abandoning a domain cannot leave its capture or borrowing guard in a surviving view.
TEST_F(drag_fixture, DestroyedDomainClearsCaptureWithoutImplicitCallbacks)
{
    begin();
    drag.end_frame();
    {
        drag_drop_session temporary;
        const std::array views{&input, &destination};
        temporary.begin_frame(1, views);
        ASSERT_TRUE(temporary.lift(input, source));
        ASSERT_EQ(input.captured(), source);
    }
    EXPECT_EQ(input.captured(), element_handle{});
    EXPECT_EQ(count(*source_handler, event_kind::drag_cancelled), 0);
    const std::array views{&input, &destination};
    drag.begin_frame(2, views);
    finish();
}
} // namespace atlas::ui::tests
