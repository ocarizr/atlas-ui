/** @file @brief Windows ICU cardinal selection with representative CLDR languages. */
#include <atlas/localization/catalog.hpp>
#include <gtest/gtest.h>

namespace atlas::localization::tests
{
/// @brief Native cardinal categories include English, Arabic, Russian and fractional operands.
TEST(native_localization, uses_native_cardinal_rules)
{
    ASSERT_TRUE(plural_backend_available());
    EXPECT_EQ(select_plural("en-US", 1), plural_category::one);
    EXPECT_EQ(select_plural("en-US", 1.5), plural_category::other);
    EXPECT_EQ(select_plural("ru", 2), plural_category::few);
    EXPECT_EQ(select_plural("ru", 5), plural_category::many);
    EXPECT_EQ(select_plural("ar", 0), plural_category::zero);
    EXPECT_EQ(select_plural("ar", 2), plural_category::two);
    EXPECT_EQ(select_plural("ar", 3), plural_category::few);
    EXPECT_EQ(select_plural("ar", 11), plural_category::many);
}
} // namespace atlas::localization::tests
