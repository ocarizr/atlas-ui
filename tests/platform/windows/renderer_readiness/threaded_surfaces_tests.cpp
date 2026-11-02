/** @file @brief Dedicated graphics coordination while main keeps multiple native windows
 * responsive. */
#include <atlas/render_host/render_loop.hpp>
#include <atlas/renderer/renderer.hpp>
#include <atomic>
#include <gtest/gtest.h>
#include <sstream>
#include <thread>

namespace atlas::renderer::tests
{
/// @brief Worker writes results; main reads only after the corresponding release publication/join.
struct surface_observations
{
    std::atomic<std::uint64_t> completed{0}; ///< Last processed identity.
    std::vector<frame_result> primary;       ///< Primary-window outcomes, in frame order.
    std::vector<frame_result> secondary;     ///< Independent secondary-window outcomes.
    std::thread::id graphics_thread;         ///< Creating and destruction coordinator.
};

/// @brief One graphics device with two leased surfaces, neither touching a main-thread facade.
class dual_surface_handler final : public render_host::frame_handler
{
  public:
    dual_surface_handler(const window::presentation_target& first,
                         const window::presentation_target& second, surface_observations& results,
                         const diagnostics::services& destinations)
        : graphics_({adapter_preference::software}, {}, destinations),
          primary_(graphics_, first, {2, false}), secondary_(graphics_, second, {2, false}),
          results_(results)
    {
        results_.graphics_thread = std::this_thread::get_id();
    } ///< Initialize exclusively on render.

    void render(const render_host::frame_snapshot& frame) override
    {
        primary_.resize(frame.window.minimized
                            ? surface_extent{}
                            : surface_extent{frame.window.width, frame.window.height});
        results_.primary.push_back(primary_.render_frame());
        results_.secondary.push_back(secondary_.render_frame());
        results_.completed.store(frame.id, std::memory_order_release);
    } ///< Suspend one surface without starving or closing the other.

    void shutdown() override
    {
        EXPECT_EQ(results_.graphics_thread, std::this_thread::get_id());
        graphics_.wait_idle();
        EXPECT_EQ(graphics_.debug_error_count(), 0U);
    } ///< Drain before handler destruction and before main releases native windows.
  private:
    device graphics_;                ///< Single queue shared by both surfaces.
    presentation_surface primary_;   ///< Independently resized presentation storage.
    presentation_surface secondary_; ///< Closing this surface does not close primary.
    surface_observations& results_;  ///< Test-owned through joined shutdown.
};

/// @brief Ensure pumping cleanup after assertions or early returns.
class surface_drain
{
  public:
    surface_drain(render_host::render_loop& loop, window::window& first, window::window& second)
        : loop_(loop), first_(first), second_(second)
    {
    } ///< Borrow only within the fixture scope.

    ~surface_drain()
    {
        try
        {
            loop_.stop_and_join(
                [&]
                {
                    pump();
                });
        }
        catch (...)
        {
            std::terminate();
        }
    } ///< Never strand DXGI behind an assertion exit.

    void pump()
    {
        first_.poll_events();
        second_.poll_events();
        first_.wait_for_events(1);
    } ///< Keep both snapshot channels current.

    bool until(const std::function<bool()>& condition)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
        while (!condition())
        {
            if (loop_.finished() || std::chrono::steady_clock::now() > deadline)
            {
                return false;
            }

            pump();
        }

        return true;
    } ///< Bounded diagnostics without arbitrary sleeps.
  private:
    render_host::render_loop& loop_; ///< Dedicated coordinator.
    window::window& first_;          ///< Main-owned window.
    window::window& second_;         ///< Main-owned window.
};

/// @brief Repeated resize/suspend/close preserves responsiveness, surface isolation and thread
/// ownership.
TEST(renderer_readiness, threaded_multi_surface_lifecycle_soak)
{
    window::window primary{{"Primary", 240, 180, false}}, secondary{{"Secondary", 240, 180, false}};
    const auto first = primary.share_presentation(), second = secondary.share_presentation();
    auto capture = std::make_shared<diagnostics::profiler>();
    surface_observations results;
    render_host::render_loop loop{[&]
                                  {
                                      return std::make_unique<dual_surface_handler>(
                                          first, second, results,
                                          diagnostics::services{{}, capture});
                                  },
                                  {},
                                  {{}, capture}};
    surface_drain cleanup{loop, primary, secondary};
    for (std::uint64_t frame = 1; frame <= 32; ++frame)
    {
        primary.resize(240 + static_cast<std::uint32_t>(frame % 4) * 16,
                       180 + static_cast<std::uint32_t>(frame % 3) * 8);
        auto state = primary.poll_events();
        state.minimized = frame % 5 == 0;
        if (frame == 16)
        {
            secondary.request_close();
        }

        ASSERT_TRUE(loop.try_submit({frame, state}));
        ASSERT_TRUE(cleanup.until(
            [&]
            {
                return results.completed.load(std::memory_order_acquire) >= frame;
            }));
    }

    loop.stop_and_join(
        [&]
        {
            cleanup.pump();
        });
    EXPECT_NE(results.graphics_thread, std::this_thread::get_id());
    EXPECT_EQ(loop.statistics().completed, 32U);
    EXPECT_LE(loop.statistics().peak_queued, 2U);
    EXPECT_EQ(results.primary[4], frame_result::suspended);
    EXPECT_EQ(results.primary.back(), frame_result::presented);
    EXPECT_EQ(results.secondary.back(), frame_result::surface_closed);
    std::ostringstream trace;
    capture->write_chrome_trace(trace);
    EXPECT_EQ(trace.str().find("render_host.frame") != std::string::npos,
              diagnostics::instrumentation_enabled);
    EXPECT_EQ(capture->statistics().errors, 0U);
}
} // namespace atlas::renderer::tests
