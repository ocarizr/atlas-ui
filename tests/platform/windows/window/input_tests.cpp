/** @file @brief Native messages produce bounded portable primary-pointer and keyboard packets. */
#include <Windows.h>
#include <atlas/window/window.hpp>
#include <gtest/gtest.h>

namespace atlas::window::tests
{
/// @brief Hidden windows allow deterministic message replay without injecting user input.
TEST(native_input_test, ReplaysSignedPointerAndSemanticKeysInMessageOrder)
{
    window target{{"Input replay fixture", 80, 60, false}};
    const auto handle = FindWindowW(L"atlas_presentation_window", L"Input replay fixture");
    ASSERT_NE(handle, nullptr);
    SendMessageW(handle, WM_MOUSEMOVE, 0, MAKELPARAM(static_cast<WORD>(-5), 12));
    SendMessageW(handle, WM_KEYDOWN, VK_RETURN, 0);
    SendMessageW(handle, WM_KEYUP, VK_RETURN, 0);
    const auto batch = target.take_input();
    ASSERT_EQ(batch.values().size(), 3);
    EXPECT_EQ(batch.values()[0].kind, input_kind::pointer_move);
    EXPECT_EQ(batch.values()[0].x, -5);
    EXPECT_EQ(batch.values()[0].y, 12);
    EXPECT_EQ(batch.values()[1].key, input_key::enter);
    EXPECT_EQ(batch.values()[2].kind, input_kind::key_up);
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

    const auto batch = target.take_input();
    EXPECT_TRUE(batch.overflow);
    ASSERT_EQ(batch.values().size(), 1);
    EXPECT_EQ(batch.values()[0].kind, input_kind::focus_lost);
    SendMessageW(handle, WM_KEYUP, VK_SPACE, 0);
    const auto next = target.take_input();
    EXPECT_FALSE(next.overflow);
    ASSERT_EQ(next.values().size(), 1);
    EXPECT_EQ(next.values()[0].kind, input_kind::key_up);
}

/// @brief Letter/digit identifiers and navigation identifiers do not leak native virtual keys.
TEST(native_input_test, MapsLettersDigitsAndNavigationToPortableIdentifiers)
{
    window target{{"Semantic keys fixture", 80, 60, false}};
    const auto handle = FindWindowW(L"atlas_presentation_window", L"Semantic keys fixture");
    ASSERT_NE(handle, nullptr);
    const std::array cases{std::pair<WPARAM, input_key>{'A', input_key::a},
                           std::pair<WPARAM, input_key>{'9', input_key::digit_9},
                           std::pair<WPARAM, input_key>{VK_LEFT, input_key::left}};
    for (const auto& [key, expected] : cases)
    {
        SendMessageW(handle, WM_KEYDOWN, key, 0);
        const auto batch = target.take_input();
        ASSERT_EQ(batch.values().size(), 1);
        EXPECT_EQ(batch.values()[0].key, expected);
    }

    SendMessageW(handle, WM_CAPTURECHANGED, 0, 0);
    SendMessageW(handle, WM_KILLFOCUS, 0, 0);
    const auto reset = target.take_input();
    ASSERT_EQ(reset.values().size(), 2);
    EXPECT_EQ(reset.values()[0].kind, input_kind::pointer_cancel);
    EXPECT_EQ(reset.values()[1].kind, input_kind::focus_lost);
}
} // namespace atlas::window::tests
