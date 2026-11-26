/** @file @brief Committed Unicode text edits remain bounded and cancel without touching assets. */
#include "input_test_support.hpp"
#include <atlas/input/text_entry.hpp>
#include <gtest/gtest.h>

namespace atlas::input::tests
{
/// @brief Append whole Unicode scalars and remove an entire UTF-8 scalar with backspace.
TEST(text_entry, UnicodeAndBackspaceAreScalarSafe)
{
    text_entry text{8};
    text.begin("A");
    EXPECT_TRUE(text.append(U'\u00e9'));
    EXPECT_TRUE(text.append(U'\U0001f600'));
    EXPECT_EQ(text.value(), "A\xc3\xa9\xf0\x9f\x98\x80");
    text.backspace();
    EXPECT_EQ(text.value(), "A\xc3\xa9");
    EXPECT_FALSE(text.append(0xd800));
    EXPECT_FALSE(text.append(U'\n'));
}

/// @brief Overflow preserves the prior value instead of truncating a multibyte scalar.
TEST(text_entry, OverflowDoesNotPartiallyAppend)
{
    text_entry text{3};
    text.begin("ab");
    EXPECT_FALSE(text.append(U'\u00e9'));
    EXPECT_EQ(text.value(), "ab");
    EXPECT_THROW(text.begin("long"), std::length_error);
    EXPECT_EQ(text.value(), "ab");
}

/// @brief Committed text cannot accidentally activate or latch a matching physical-key binding.
TEST(text_entry, CommittedTextDoesNotBecomeAnActionControl)
{
    input_system system;
    system.define_action({1, "physical"});
    const std::array bindings{binding{1, {device_kind::keyboard, 65}}};
    system.define_context({1, "keys", 0, true, activation_policy::allow_held, bindings});
    recorder listener;
    const event text{.kind = event_kind::text_input,
                     .device = {1, device_kind::keyboard},
                     .control = 65,
                     .value = {1, 0}};
    system.dispatch(1, 0, {&text, 1}, listener);
    EXPECT_EQ(listener.raw.size(), 1);
    EXPECT_TRUE(listener.actions.empty());
    system.dispatch(2, 1, {}, listener);
    EXPECT_TRUE(listener.actions.empty());
}
} // namespace atlas::input::tests
