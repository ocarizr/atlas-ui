/** @file @brief Platform-neutral window configuration invariants. */
#include <atlas/window/window.hpp>
#include <gtest/gtest.h>

namespace atlas::window::tests
{
/// @brief Invalid initial client extents must fail before platform allocation.
class invalid_window_test : public ::testing::TestWithParam<std::uint32_t>
{
};

/// @brief Zero and unrepresentable window sizes are rejected independently of platform support.
TEST_P(invalid_window_test, rejects_invalid_initial_size)
{
    window_options options;
    options.width = GetParam();
    EXPECT_THROW(options.validate(), std::invalid_argument);
    EXPECT_THROW(window{options}, std::invalid_argument);
}

INSTANTIATE_TEST_SUITE_P(bounds, invalid_window_test, ::testing::Values(0U, 0xffffffffU));

/// @brief Embedded terminators cannot silently change the title passed to a native API.
TEST(window_contract_tests, rejects_ambiguous_title)
{
    window_options options;
    options.title = std::string{"Atlas\0hidden", 12};
    EXPECT_THROW(options.validate(), std::invalid_argument);
}

/// @brief Valid configuration is independent of whether a native backend was selected.
TEST(window_contract_tests, accepts_default_configuration)
{
    EXPECT_NO_THROW(window_options{}.validate());
    if (!backend_available())
    {
        EXPECT_THROW(window{}, std::runtime_error);
    }
}
} // namespace atlas::window::tests
