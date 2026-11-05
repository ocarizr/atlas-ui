/** @file @brief Native messages produce bounded portable primary-pointer and keyboard packets. */
#include <Windows.h>
#include <atlas/input/key.hpp>
#include <atlas/window/window.hpp>
#include <gtest/gtest.h>

namespace atlas::window::tests
{
namespace
{
/// @brief Keep multiple bounded native recordings off the test runner's stack.
std::unique_ptr<input::event_batch> drain(window& target)
{
    return std::make_unique<input::event_batch>(target.take_input());
}
} // namespace

/// @brief Hidden windows allow deterministic message replay without injecting user input.
TEST(native_input_test, ReplaysSignedPointerAndSemanticKeysInMessageOrder)
{
    window target{{"Input replay fixture", 80, 60, false}};
    const auto handle = FindWindowW(L"atlas_presentation_window", L"Input replay fixture");
    ASSERT_NE(handle, nullptr);
    SendMessageW(handle, WM_MOUSEMOVE, 0, MAKELPARAM(static_cast<WORD>(-5), 12));
    SendMessageW(handle, WM_KEYDOWN, VK_RETURN, 0);
    SendMessageW(handle, WM_KEYUP, VK_RETURN, 0);
    const auto batch = drain(target);
    ASSERT_EQ(batch->values().size(), 3);
    EXPECT_EQ(batch->values()[0].kind, input::event_kind::pointer_motion);
    EXPECT_EQ(batch->values()[0].value.x, -5);
    EXPECT_EQ(batch->values()[0].value.y, 12);
    EXPECT_EQ(batch->values()[1].control, static_cast<std::uint32_t>(input::key::enter));
    EXPECT_EQ(batch->values()[2].kind, input::event_kind::control_changed);
    EXPECT_TRUE(target.take_input().values().empty());
}

/// @brief Overflow fails safe with an explicit reset instead of losing a release silently.
TEST(native_input_test, OverflowPublishesOneResetAndRecoversAtTheNextDrain)
{
    window target{{"Input overflow fixture", 80, 60, false}};
    const auto handle = FindWindowW(L"atlas_presentation_window", L"Input overflow fixture");
    ASSERT_NE(handle, nullptr);
    for (unsigned i = 0; i < 300; ++i)
    {
        SendMessageW(handle, WM_KEYDOWN, VK_SPACE, 0);
    }

    const auto batch = drain(target);
    EXPECT_TRUE(batch->overflow);
    ASSERT_EQ(batch->values().size(), 1);
    EXPECT_EQ(batch->values()[0].kind, input::event_kind::source_reset);
    SendMessageW(handle, WM_KEYUP, VK_SPACE, 0);
    const auto next = drain(target);
    EXPECT_FALSE(next->overflow);
    ASSERT_EQ(next->values().size(), 1);
    EXPECT_EQ(next->values()[0].kind, input::event_kind::control_changed);
}

/// @brief Letter/digit identifiers and navigation identifiers do not leak native virtual keys.
TEST(native_input_test, MapsLettersDigitsAndNavigationToPortableIdentifiers)
{
    window target{{"Semantic keys fixture", 80, 60, false}};
    const auto handle = FindWindowW(L"atlas_presentation_window", L"Semantic keys fixture");
    ASSERT_NE(handle, nullptr);
    const std::array cases{std::pair<WPARAM, input::key>{'A', input::key::a},
                           std::pair<WPARAM, input::key>{'9', input::key::digit_9},
                           std::pair<WPARAM, input::key>{VK_LEFT, input::key::left}};
    for (const auto& [key, expected] : cases)
    {
        SendMessageW(handle, WM_KEYDOWN, key, 0);
        const auto batch = drain(target);
        ASSERT_EQ(batch->values().size(), 1);
        EXPECT_EQ(batch->values()[0].control, static_cast<std::uint32_t>(expected));
    }

    SendMessageW(handle, WM_CAPTURECHANGED, 0, 0);
    SendMessageW(handle, WM_KILLFOCUS, 0, 0);
    const auto reset = drain(target);
    ASSERT_EQ(reset->values().size(), 2);
    EXPECT_EQ(reset->values()[0].kind, input::event_kind::pointer_cancel);
    EXPECT_EQ(reset->values()[1].kind, input::event_kind::focus_lost);
}
} // namespace atlas::window::tests
