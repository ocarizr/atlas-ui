/** @file @brief Renderer configuration and frame-data validation without graphics SDK types. */
#include <atlas/renderer/renderer.hpp>
#include <gtest/gtest.h>
#include <limits>

namespace atlas::renderer::tests
{
/// @brief Bound buffering explicitly rather than allowing unbounded CPU/GPU backlog.
class invalid_surface_test : public ::testing::TestWithParam<std::uint32_t>
{
};

/// @brief Supported frame counts are two or three.
TEST_P(invalid_surface_test, rejects_invalid_frame_count)
{
    surface_options options;
    options.frame_count = GetParam();
    EXPECT_THROW(options.validate(), std::invalid_argument);
}

INSTANTIATE_TEST_SUITE_P(bounds, invalid_surface_test, ::testing::Values(0U, 1U, 4U));

/// @brief Nonfinite colors fail before recording native commands.
TEST(renderer_contract_tests, rejects_nonfinite_frame_data)
{
    frame_description frame;
    frame.clear_color[1] = std::numeric_limits<float>::quiet_NaN();
    EXPECT_THROW(frame.validate(), std::invalid_argument);
}

/// @brief Positive diagnostic timeout is required before allocating native objects.
TEST(renderer_contract_tests, rejects_zero_gpu_timeout)
{
    device_options options;
    options.gpu_wait_timeout_ms = 0;
    EXPECT_THROW(device{options}, std::invalid_argument);
}

/// @brief Invalid policies must fail before allocating native graphics objects.
TEST(renderer_contract_tests, rejects_invalid_device_policies)
{
    device_options options;
    // Deliberate invalid input verifies rejection before native allocation.
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    options.adapter = static_cast<adapter_preference>(99);
    EXPECT_THROW(device{options}, std::invalid_argument);
    options = {};
    // Deliberate invalid input verifies rejection before native allocation.
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    options.debug = static_cast<debug_validation>(99);
    EXPECT_THROW(device{options}, std::invalid_argument);
    options = {};
    options.gpu_wait_timeout_ms = 0xffffffffU;
    EXPECT_THROW(device{options}, std::invalid_argument);
}

/// @brief The SDK-free profile explicitly reports unsupported graphics rather than pretending to
/// draw.
TEST(renderer_contract_tests, reports_unavailable_backend)
{
    if (!backend_available())
    {
        EXPECT_THROW(device{}, std::runtime_error);
    }
}
} // namespace atlas::renderer::tests
