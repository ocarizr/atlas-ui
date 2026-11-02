/** @file @brief Main-owned windows publish immutable snapshots to another coordinator. */
#include <atlas/window/window.hpp>
#include <future>
#include <gtest/gtest.h>

namespace atlas::window::tests
{
/// @brief Snapshot consumers never inspect mutable WNDPROC storage on the render thread.
TEST(presentation_target_test, publishes_resize_and_close_across_threads)
{
    window target{{"Shared window", 80, 60, false}};
    const auto lease = target.share_presentation();
    target.resize(48, 32);
    const auto owner_state = target.poll_events();
    const auto size = std::async(std::launch::async,
                                 [&]
                                 {
                                     return lease.state();
                                 })
                          .get();
    EXPECT_EQ(size.width, owner_state.width);
    EXPECT_EQ(size.height, owner_state.height);
    target.request_close();
    const auto closed = std::async(std::launch::async,
                                   [&]
                                   {
                                       return lease.state();
                                   })
                            .get();
    EXPECT_TRUE(closed.close_requested);
}
} // namespace atlas::window::tests
