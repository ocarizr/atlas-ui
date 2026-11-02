/** @file @brief WARP integration tests verify real GPU pixels and native lifetimes. */
#include <atlas/renderer/renderer.hpp>
#include <atlas/window/window.hpp>
#include <future>
#include <gtest/gtest.h>

namespace atlas::renderer::tests
{
/// @brief Each case owns an isolated hidden window, software device and drained surface.
class presentation_test : public ::testing::Test
{
  protected:
    window::window target{{"Atlas integration", 160, 120, false}}; ///< Independent native client.
    device graphics{{adapter_preference::software}}; ///< Deterministic software graphics backend.
    presentation_surface surface{
        graphics, target, {2, false}}; ///< Bounded independently fenced slots.

    /// @brief Signal and wait for all earlier commands on the graphics queue.
    void TearDown() override
    {
        graphics.wait_idle();
        EXPECT_EQ(graphics.debug_error_count(), 0U);
    }
};

/// @brief Exercise both supported buffering depths with independent per-test native lifetimes.
class buffering_test : public ::testing::TestWithParam<std::uint32_t>
{
};

/// @brief Reusing completed allocators preserves the latest frame across wraparound.
TEST_P(buffering_test, wraps_frame_slots_with_preserved_readback)
{
    window::window target{{"Buffering", 83, 61, false}};
    device graphics{{adapter_preference::software}};
    presentation_surface surface{graphics, target, {GetParam(), false}};
    for (std::uint32_t frame = 0; frame < GetParam() * 3; ++frame)
    {
        surface.render_frame({{0, 1, 0, 1}, false});
    }

    EXPECT_EQ(surface.readback_last_frame().rgba[1], 255);
    graphics.wait_idle();
    EXPECT_EQ(graphics.debug_error_count(), 0U);
}

INSTANTIATE_TEST_SUITE_P(supported, buffering_test, ::testing::Values(2U, 3U));

/// @brief Readback includes row-pitch padding removal and normalized color conversion.
TEST_F(presentation_test, reads_actual_clear_pixels)
{
    EXPECT_NO_THROW(surface.render_frame({{0.25F, 0.5F, 0.75F, 1}, false}));
    const auto image = surface.readback_last_frame();
    ASSERT_EQ(image.rgba.size(), 160U * 120U * 4U);
    for (std::size_t pixel = 0; pixel < image.rgba.size(); pixel += 4)
    {
        ASSERT_NEAR(image.rgba[pixel], 64, 1);
        ASSERT_NEAR(image.rgba[pixel + 1], 128, 1);
        ASSERT_NEAR(image.rgba[pixel + 2], 191, 1);
        ASSERT_EQ(image.rgba[pixel + 3], 255);
    }
}

/// @brief Rasterization changes the triangle interior and preserves a clear corner.
TEST_F(presentation_test, draws_triangle_and_reuses_completed_frame_slots)
{
    for (int frame = 0; frame < 8; ++frame)
    {
        surface.render_frame({{0, 0, 0, 1}, true});
    }

    const auto image = surface.readback_last_frame();
    const auto center = (60U * 160U + 80U) * 4U;
    EXPECT_GT(image.rgba[center], 20);
    EXPECT_GT(image.rgba[center + 1], 20);
    EXPECT_GT(image.rgba[center + 2], 20);
    EXPECT_EQ(image.rgba[0], 0);
    EXPECT_EQ(surface.submitted_frames(), 8U);
    EXPECT_TRUE(graphics.info().software);
}

/// @brief Zero-size suspension does not submit work; resize invalidates stale captures.
TEST_F(presentation_test, resizes_suspends_and_restores)
{
    EXPECT_THROW(surface.readback_last_frame(), std::logic_error);
    surface.render_frame();
    surface.resize({0, 0});
    EXPECT_EQ(surface.render_frame(), frame_result::suspended);
    EXPECT_EQ(surface.submitted_frames(), 1U);
    EXPECT_THROW(surface.readback_last_frame(), std::logic_error);
    target.resize(173, 97);
    target.poll_events();
    surface.resize({173, 97});
    surface.render_frame();
    const auto image = surface.readback_last_frame();
    EXPECT_EQ(image.extent.width, 173U);
    EXPECT_EQ(image.rgba.size(), 173U * 97U * 4U);
}

/// @brief Rejected operations cannot enqueue GPU work or alter presentation state.
TEST_F(presentation_test, rejects_invalid_input_and_foreign_threads)
{
    EXPECT_THROW(surface.resize({16385, 1}), std::invalid_argument);
    EXPECT_THROW(surface.render_frame({{-1, 0, 0, 1}, false}), std::invalid_argument);
    EXPECT_EQ(surface.submitted_frames(), 0U);
    auto foreign = std::async(std::launch::async,
                              [this]
                              {
                                  EXPECT_THROW(surface.render_frame(), std::logic_error);
                                  EXPECT_THROW(target.poll_events(), std::logic_error);
                                  EXPECT_THROW(graphics.wait_idle(), std::logic_error);
                              });
    foreign.get();
    surface.render_frame();
}

/// @brief Native close is explicit and prevents subsequent submission.
TEST_F(presentation_test, closing_surface_preserves_other_windows)
{
    window::window other{{"Other", 80, 60, false}};
    target.request_close();
    EXPECT_EQ(surface.render_frame(), frame_result::surface_closed);
    EXPECT_FALSE(other.poll_events().close_requested);
    EXPECT_EQ(surface.submitted_frames(), 0U);
}

/// @brief Surface pins native resources even if both public facades are released first.
TEST(renderer_lifetime_tests, retains_device_window_and_pmr_owner)
{
    auto memory = std::make_shared<std::pmr::synchronized_pool_resource>();
    std::weak_ptr<std::pmr::memory_resource> lifetime = memory;
    auto target =
        std::make_unique<window::window>(window::window_options{"Retained", 80, 60, false});
    auto graphics = std::make_unique<device>(device_options{adapter_preference::software}, memory);
    auto surface = std::make_unique<presentation_surface>(*graphics, *target);
    memory.reset();
    surface->render_frame();
    graphics.reset();
    target.reset();
    EXPECT_FALSE(lifetime.expired());
    EXPECT_EQ(surface->render_frame(), frame_result::surface_closed);
    surface.reset();
    EXPECT_TRUE(lifetime.expired());
}
} // namespace atlas::renderer::tests
