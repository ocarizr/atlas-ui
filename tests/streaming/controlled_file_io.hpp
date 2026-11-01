/** @file @brief Controlled pending storage independent of native or portable file APIs. */
#pragma once
#include "streamer_state.hpp"
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

namespace atlas::streaming::tests
{
/// @brief A storage owner that completes requests only when the test explicitly releases them.
class controlled_file_io final : public detail::file_io
{
  public:
    /// @brief Record actual concurrent submissions, then retain them as pending storage work.
    std::optional<read_result> start(const std::shared_ptr<detail::read_operation>& work,
                                     const std::filesystem::path&) override
    {
        const std::lock_guard lock{mutex};
        ++starts;
        pending.push_back(work);
        peak = std::max(peak, pending.size());
        changed.notify_all();
        return std::nullopt;
    }

    /// @brief Controlled wait with a persistent notification prevents lost admission wakeups.
    std::optional<detail::file_completion> poll(bool block) noexcept override
    try
    {
        std::unique_lock lock{mutex};
        if (block)
        {
            changed.wait(lock,
                         [&]
                         {
                             return notified || release_all || releases != 0;
                         });
        }

        notified = false;
        if (pending.empty() || (!release_all && releases == 0))
        {
            return std::nullopt;
        }

        if (releases != 0)
        {
            --releases;
        }

        auto work = std::move(pending.front());
        pending.pop_front();
        lock.unlock();
        try
        {
            auto bytes = detail::allocate_buffer(work->reservation);
            return detail::file_completion{std::move(work),
                                           std::shared_ptr<const asset_bytes>{std::move(bytes)}};
        }
        catch (...)
        {
            return detail::file_completion{std::move(work), std::current_exception()};
        }
    }

    catch (...)
    {
        std::terminate();
    }

    /// @brief Wake blocked polling after enqueue, stop request or shutdown.
    void wake() noexcept override
    {
        const std::lock_guard lock{mutex};
        notified = true;
        changed.notify_all();
    }

    /// @brief Wait for a known number of submissions with a diagnostic timeout.
    bool await_starts(std::size_t count)
    {
        std::unique_lock lock{mutex};
        return changed.wait_for(lock, std::chrono::seconds{3},
                                [&]
                                {
                                    return starts >= count;
                                });
    }

    /// @brief Release one native slot or all current and future work during fixture cleanup.
    void release(bool all = false)
    {
        const std::lock_guard lock{mutex};
        ++releases;
        release_all = all;
        changed.notify_all();
    }

    /// @brief Protect the test controller and owner-thread observations.
    std::mutex mutex;
    /// @brief Waiters for submission observations and explicit completions.
    std::condition_variable changed;
    /// @brief Requests retained as pending storage operations.
    std::deque<std::shared_ptr<detail::read_operation>> pending;
    /// @brief Total submissions and maximum actual pending requests.
    std::size_t starts{0}, peak{0};
    /// @brief Number of completion permits, independent of wake notifications.
    std::size_t releases{0};
    /// @brief Persistent wake and cleanup flags.
    bool notified{false}, release_all{false};
};

/// @brief Exercise the real bounded pump with one CPU worker and explicitly controlled storage.
class file_pump_test : public ::testing::Test
{
  protected:
    /// @brief Start a pump with two available native I/O slots.
    void SetUp() override
    {
        auto storage = std::make_unique<controlled_file_io>();
        files = storage.get();
        streamer_limits limits;
        limits.max_io_requests = 2;
        state = std::make_shared<detail::streamer_state>(
            jobs, asset_catalog{{"test", loose_file_asset{"unused"}}}, limits, nullptr,
            std::move(storage));
        pump = std::jthread{[owner = state]
                            {
                                owner->run_io();
                            }};
    }

    /// @brief Cleanup releases held I/O even after failed assertions, then joins before
    /// destruction.
    void TearDown() override
    {
        {
            const std::lock_guard lock{state->mutex};
            state->accepting = false;
        }
        files->release(true);
        files->wake();
        pump.join();
        jobs.wait_idle();
        EXPECT_EQ(state->pending, 0);
        EXPECT_EQ(state->io_in_flight.load(), 0);
        EXPECT_LE(files->peak, 2);
    }

    /// @brief Admit a request using the same quota, wake capability and ready-delivery path.
    std::future<void> enqueue(const std::stop_token& cancellation = {})
    {
        auto work = std::make_shared<detail::read_operation>();
        work->request = {"test", 0, 4};
        work->options.cancellation = cancellation;
        work->completion = [cancellation](const read_result& result)
        {
            if (cancellation.stop_requested())
            {
                ASSERT_TRUE(std::holds_alternative<std::exception_ptr>(result));
                EXPECT_THROW(std::rethrow_exception(std::get<std::exception_ptr>(result)),
                             jobs::job_cancelled);
            }
            else
            {
                EXPECT_TRUE(std::holds_alternative<std::shared_ptr<const asset_bytes>>(result));
            }
        };
        work->reservation = detail::reserve_buffer(state->memory, 4, 1024);
        work->cancellation_wakeup.emplace(cancellation, detail::io_wakeup{state});
        auto future = work->finished.get_future();
        {
            const std::lock_guard lock{state->mutex};
            state->queue.push_back(std::move(work));
            ++state->pending;
        }
        files->wake();
        return future;
    }

    /// @brief Application-owned CPU pool; held I/O must never occupy its only worker.
    jobs::job_system jobs{1};
    /// @brief Shared real streamer state containing the controlled storage adapter.
    std::shared_ptr<detail::streamer_state> state;
    /// @brief Borrowed controller valid while shared state retains the adapter.
    controlled_file_io* files{};
    /// @brief Persistent I/O owner joined by teardown.
    std::jthread pump;
};
} // namespace atlas::streaming::tests
