/** @file @brief Frame/shutdown failures preserve original causes and complete handler cleanup. */
#include <atlas/render_host/render_loop.hpp>
#include <atomic>
#include <future>
#include <gtest/gtest.h>
#include <thread>

namespace atlas::render_host::tests
{
/// @brief Select one isolated failure context without interchangeable boolean switches.
enum class failure_phase : std::uint8_t
{
    render,  ///< Fail while consuming an accepted snapshot.
    shutdown ///< Fail while draining an otherwise successful session.
};

/// @brief Throw only at the selected boundary, recording destruction after the original failure.
class failing_handler final : public frame_handler
{
  public:
    failing_handler(failure_phase phase, bool& destroyed) : phase_(phase), destroyed_(destroyed)
    {
    } ///< Bind through joined shutdown.

    ~failing_handler() override
    {
        destroyed_ = true;
    } ///< Main observes only after join.

    void render(const frame_snapshot&) override
    {
        if (phase_ == failure_phase::render)
        {
            throw std::runtime_error("render failure");
        }
    } ///< Controlled frame failure.

    void shutdown() override
    {
        if (phase_ == failure_phase::shutdown)
        {
            throw std::runtime_error("shutdown failure");
        }
    } ///< Controlled drain failure.
  private:
    failure_phase phase_; ///< Immutable boundary selection.
    bool& destroyed_;     ///< Main-owned storage retained until after thread join.
};

/// @brief Parameterized facts keep shared ownership/cleanup assertions in one place.
class failure_test : public ::testing::TestWithParam<failure_phase>
{
};

/// @brief A render failure is never mistaken for successful CPU or GPU completion.
TEST_P(failure_test, propagates_original_cause_after_destruction)
{
    bool destroyed = false;
    render_loop loop{[&]
                     {
                         return std::make_unique<failing_handler>(GetParam(), destroyed);
                     }};
    ASSERT_TRUE(loop.try_submit({1, {}}));
    try
    {
        loop.stop_and_join(
            []
            {
                std::this_thread::yield();
            });
        FAIL() << "expected a controlled render lifecycle failure";
    }
    catch (const std::runtime_error& error)
    {
        EXPECT_EQ(std::string{error.what()},
                  GetParam() == failure_phase::render ? "render failure" : "shutdown failure");
    }

    EXPECT_TRUE(loop.finished());
    EXPECT_TRUE(destroyed);
    const auto state = loop.statistics();
    EXPECT_EQ(state.accepted, state.completed + state.failed + state.discarded);
    EXPECT_FALSE(loop.try_submit({2, {}}));
}

INSTANTIATE_TEST_SUITE_P(lifecycle, failure_test,
                         ::testing::Values(failure_phase::render, failure_phase::shutdown));

/// @brief A null factory result fails on the render thread rather than invoking an empty handler.
TEST(render_failure, rejects_null_handler)
{
    render_loop loop{[]
                     {
                         return std::unique_ptr<frame_handler>{};
                     }};
    EXPECT_THROW(loop.stop_and_join(
                     []
                     {
                         std::this_thread::yield();
                     }),
                 std::runtime_error);
    EXPECT_TRUE(loop.finished());
}

/// @brief Submission identity and thread guards run before queue mutation.
TEST(render_failure, validates_frame_identity_and_main_thread_admission)
{
    bool destroyed = false;
    render_loop loop{[&]
                     {
                         return std::make_unique<failing_handler>(failure_phase::shutdown,
                                                                  destroyed);
                     }};
    EXPECT_THROW(loop.try_submit({0, {}}), std::invalid_argument);
    ASSERT_TRUE(loop.try_submit({1, {}}));
    EXPECT_THROW(loop.try_submit({1, {}}), std::invalid_argument);
    auto foreign = std::async(std::launch::async,
                              [&]
                              {
                                  EXPECT_THROW(loop.try_submit({2, {}}), std::logic_error);
                              });
    foreign.get();
    EXPECT_THROW(loop.stop_and_join(
                     []
                     {
                         std::this_thread::yield();
                     }),
                 std::runtime_error);
    EXPECT_EQ(loop.statistics().accepted, 1U);
    EXPECT_TRUE(destroyed);
}

/// @brief CPU payload storage may have independent ownership after its source facade disappears.
class payload final : public frame_payload
{
};

/// @brief Completed frame slots release CPU packets before loop destruction or slot reuse.
TEST(render_failure, completed_snapshots_release_payloads_after_join)
{
    bool destroyed = false;
    render_loop loop{[&]
                     {
                         return std::make_unique<failing_handler>(failure_phase::shutdown,
                                                                  destroyed);
                     }};
    auto data = std::make_shared<payload>();
    const std::weak_ptr<const frame_payload> lifetime{data};
    ASSERT_TRUE(loop.try_submit({1, {}, data}));
    data.reset();
    EXPECT_THROW(loop.stop_and_join(
                     []
                     {
                         std::this_thread::yield();
                     }),
                 std::runtime_error);
    EXPECT_TRUE(lifetime.expired());
}

/// @brief Discarded packet cleanup may query host observations without reentering a held lock.
class observed_payload final : public frame_payload
{
  public:
    explicit observed_payload(std::function<void()> released) : released_(std::move(released))
    {
    } ///< Retain test-only cleanup observation.

    ~observed_payload() override
    {
        released_();
    } ///< Execute only after queue locks have released.
  private:
    std::function<void()> released_; ///< Borrowed test data outlives joined shutdown.
};

/// @brief Release before the loop destructor even when an assertion exits the test.
class promise_release
{
  public:
    explicit promise_release(std::promise<void>& signal) : signal_(signal)
    {
    } ///< Isolated gate owner.

    ~promise_release()
    {
        open();
    } ///< Guarantee cleanup after failed assertions.

    void open() noexcept
    {
        if (!opened_)
        {
            opened_ = true;
            try
            {
                signal_.set_value();
            }
            catch (...)
            {
                std::terminate();
            }
        }
    } ///< Idempotent release from the test's main thread.
  private:
    std::promise<void>& signal_; ///< Stable future through drain.
    bool opened_ = false;        ///< Main-only state.
};

/// @brief Initialization failure discards accepted CPU packets and releases them before finished().
TEST(render_failure, discards_payloads_outside_queue_lock)
{
    std::promise<void> release;
    std::atomic<bool> released{false};
    const auto gate = release.get_future().share();
    render_loop loop{[&]() -> std::unique_ptr<frame_handler>
                     {
                         if (gate.wait_for(std::chrono::seconds{5}) != std::future_status::ready)
                         {
                             throw std::runtime_error("factory gate timed out");
                         }

                         throw std::runtime_error("controlled factory failure");
                     }};
    promise_release cleanup{release};
    auto data = std::make_shared<observed_payload>(
        [&]
        {
            EXPECT_EQ(loop.statistics().discarded, 2U);
            released.store(true);
        });
    ASSERT_TRUE(loop.try_submit({1, {}, data}));
    ASSERT_TRUE(loop.try_submit({2, {}, data}));
    data.reset();
    EXPECT_THROW(loop.stop_and_join(
                     [&]
                     {
                         cleanup.open();
                     }),
                 std::runtime_error);
    EXPECT_TRUE(released.load());
    EXPECT_EQ(loop.statistics().discarded, 2U);
}
} // namespace atlas::render_host::tests
