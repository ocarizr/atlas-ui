/** @file @brief Owned typed payloads remain independent from source buffers. */
#include <atlas/ui/drag_drop.hpp>
#include <gtest/gtest.h>

namespace atlas::ui::tests
{
/// @brief Source buffers may be destroyed or mutated without changing the offered bytes.
TEST(drag_payload_test, OwnsTextAndRetainedCopies)
{
    std::string value = "stable-asset-id";
    auto payload = drag_payload::text("atlas.asset", value);
    value.assign("changed");
    auto retained = payload;
    payload = {};
    EXPECT_EQ(retained.type(), "atlas.asset");
    EXPECT_EQ(retained.text(), "stable-asset-id");
    EXPECT_TRUE(retained.is_text());
}

/// @brief Binary data is not implicitly treated as a Lua string or filesystem path.
TEST(drag_payload_test, DistinguishesBinaryAndEmptyText)
{
    const std::array bytes{std::byte{0}, std::byte{255}};
    const auto binary = drag_payload::binary("application.test", bytes);
    EXPECT_EQ(binary.bytes().size(), 2);
    EXPECT_FALSE(binary.is_text());
    EXPECT_TRUE(binary.text().empty());
    const auto text = drag_payload::text("application.test", "");
    EXPECT_TRUE(text);
    EXPECT_TRUE(text.is_text());
}

/// @brief Invalid identifiers, excessive bytes and malformed UTF-8 fail before offering a gesture.
TEST(drag_payload_test, RejectsInvalidTypesAndBudgets)
{
    EXPECT_THROW((void)drag_payload::text("", "data"), std::invalid_argument);
    EXPECT_THROW((void)drag_payload::text("contains space", "data"), std::invalid_argument);
    EXPECT_THROW((void)drag_payload::text("data", std::string(65537, 'x')), std::length_error);
    EXPECT_THROW((void)drag_payload::text("data", std::string(1, '\0')), std::invalid_argument);
}
} // namespace atlas::ui::tests
