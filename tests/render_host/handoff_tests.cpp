/** @file @brief Deterministic queue saturation, FIFO order, thread ownership and failure cleanup.
 */
#include <atlas/render_host/render_loop.hpp>
#include <future>
#include <gtest/gtest.h>
#include <thread>

namespace atlas::render_host::tests
{
/// @brief Shared observations are read only after the render thread joins.
struct observations
{
    std::promise<void> entered; ///< Signal worker entry without sleeps.
    std::promise<void> release; ///< Main controls when the first render may finish.
    std::shared_future<void> gate = release.get_future().share(); ///< Stable worker gate.
    std::vector<std::uint64_t> frames; ///< FIFO order observed on the worker only.
    std::thread::id constructed;       ///< Factory execution thread.
    std::thread::id destroyed;         ///< Handler destruction thread.
};

/// @brief One controlled render body followed by inexpensive queued snapshots.
class observed_handler final : public frame_handler
{
  public:
    explicit observed_handler(observations& state) : state_(state)
    {
        state_.constructed = std::this_thread::get_id();
    } ///< Observe factory affinity.

    ~observed_handler() override
    {
        state_.destroyed = std::this_thread::get_id();
    } ///< Observe cleanup affinity.

    void render(const frame_snapshot& frame) override
    {
        if (state_.frames.empty())
        {
            state_.entered.set_value();
            if (state_.gate.wait_for(std::chrono::seconds{5}) != std::future_status::ready)
            {
                throw std::runtime_error("render test gate timed out");
            }
        }

        state_.frames.push_back(frame.id);
    } ///< No main-thread storage is mutated concurrently with observation.
  private:
    observations& state_; ///< Fixture-owned until after loop drain.
};

/// @brief Open a controlled gate even when an assertion leaves the test early.
class gate_release
{
  public:
    explicit gate_release(observations& state) : state_(state)
    {
    } ///< Borrow through loop drain.

    ~gate_release()
    {
        open();
    } ///< Run before the loop's joining destructor.

    void open() noexcept
    {
        if (!open_)
        {
            open_ = true;
            try
            {
                state_.release.set_value();
            }
            catch (...)
            {
                std::terminate();
            }
        }
    } ///< Idempotent main-thread gate release.
  private:
    observations& state_; ///< Isolated promise/gate owner.
    bool open_ = false;   ///< Only the test owner changes this flag.
};

/// @brief Saturation never blocks the producer, and accepted work drains in submission order.
TEST(render_handoff, bounds_lead_and_preserves_fifo_and_owner_cleanup)
{
    observations state;
    auto entered = state.entered.get_future();
    render_loop loop{[&]
                     {
                         return std::make_unique<observed_handler>(state);
                     },
                     {1, 15000}};
    gate_release release{state};
    ASSERT_TRUE(loop.try_submit({1, {}}));
    EXPECT_EQ(entered.wait_for(std::chrono::seconds{5}), std::future_status::ready);
    EXPECT_TRUE(loop.try_submit({2, {}}));
    EXPECT_FALSE(loop.try_submit({3, {}}));
    EXPECT_EQ(loop.statistics().peak_queued, 1U);
    release.open();
    loop.stop_and_join(
        []
        {
            std::this_thread::yield();
        });
    EXPECT_TRUE(loop.finished());
    EXPECT_EQ(state.frames, (std::vector<std::uint64_t>{1, 2}));
    EXPECT_NE(state.constructed, std::this_thread::get_id());
    EXPECT_EQ(state.constructed, state.destroyed);
    EXPECT_EQ(loop.statistics().completed, 2U);
}

/// @brief Initialization failures are retained and propagated after safe worker termination.
TEST(render_handoff, propagates_factory_failure_without_accepting_more_work)
{
    render_loop loop{[]() -> std::unique_ptr<frame_handler>
                     {
                         throw std::invalid_argument("factory failure");
                     }};
    EXPECT_THROW(loop.stop_and_join(
                     []
                     {
                         std::this_thread::yield();
                     }),
                 std::invalid_argument);
    EXPECT_TRUE(loop.finished());
    EXPECT_FALSE(loop.try_submit({1, {}}));
}

/// @brief Invalid capacities and duplicate frame IDs never alter accepted order.
TEST(render_handoff, validates_admission_configuration)
{
    EXPECT_THROW((render_loop{frame_factory{}}), std::invalid_argument);
    EXPECT_THROW((render_loop{[]
                              {
                                  return std::unique_ptr<frame_handler>{};
                              },
                              {0, 1}}),
                 std::invalid_argument);
}

/// @brief Draining services the producer event pump before joining a waiting render coordinator.
TEST(render_handoff, keeps_pumping_during_worker_shutdown)
{
    observations state;
    auto entered = state.entered.get_future();
    render_loop loop{[&]
                     {
                         return std::make_unique<observed_handler>(state);
                     }};
    gate_release release{state};
    ASSERT_TRUE(loop.try_submit({1, {}}));
    ASSERT_EQ(entered.wait_for(std::chrono::seconds{5}), std::future_status::ready);
    std::size_t pumps = 0;
    loop.stop_and_join(
        [&]
        {
            ++pumps;
            EXPECT_FALSE(loop.try_submit({2, {}}));
            release.open();
        });
    EXPECT_GT(pumps, 0U);
    EXPECT_EQ(loop.statistics().completed, 1U);
}
} // namespace atlas::render_host::tests
