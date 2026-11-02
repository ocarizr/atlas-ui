/** @file @brief Native client extents, close isolation and owner-thread contracts. */
#include <atlas/window/window.hpp>
#include <future>
#include <gtest/gtest.h>

namespace atlas::window::tests
{
/// @brief Hidden clients report physical dimensions even before any show message.
TEST(native_window_tests, creates_and_resizes_hidden_client)
{
    window target{{"Hidden client", 173, 97, false}};
    EXPECT_EQ(target.state().width, 173U);
    EXPECT_EQ(target.state().height, 97U);
    EXPECT_GT(target.state().dpi_scale, 0);
    target.resize(251, 139);
    EXPECT_EQ(target.poll_events().width, 251U);
    EXPECT_EQ(target.state().height, 139U);
    EXPECT_THROW(target.resize(0, 1), std::invalid_argument);
    EXPECT_EQ(target.state().width, 251U);
}

/// @brief Closing one window cannot enqueue process-wide termination for another.
TEST(native_window_tests, close_is_local_and_foreign_thread_access_fails)
{
    window first{{"First", 80, 60, false}};
    window second{{"Second", 80, 60, false}};
    auto task = std::async(std::launch::async,
                           [&first]
                           {
                               EXPECT_THROW(first.request_close(), std::logic_error);
                           });
    task.get();
    EXPECT_FALSE(first.state().close_requested);
    first.request_close();
    EXPECT_TRUE(first.poll_events().close_requested);
    EXPECT_FALSE(second.poll_events().close_requested);
}

/// @brief Invalid UTF-8 cannot be silently substituted in a native title.
TEST(native_window_tests, rejects_invalid_utf8_title)
{
    EXPECT_THROW(window(window_options{std::string(1, static_cast<char>(0xff))}),
                 std::system_error);
}
} // namespace atlas::window::tests
