/** @file @brief Bounded handoff state retained until coordinator destruction completes. */
#pragma once
#include <atlas/render_host/render_loop.hpp>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace atlas::render_host
{
/// @brief Main owns admission/lifecycle; worker owns handler/graphics, sharing only queue metadata.
class render_loop::impl
{
  public:
    /// @brief Reserve the bounded ring before starting its thread.
    impl(loop_options policy, diagnostics::services services)
        : options(policy), destinations(std::move(services)), queue(options.capacity)
    {
    }

    void check_owner() const
    {
        if (owner != std::this_thread::get_id())
        {
            throw std::logic_error("render loop requires its main owner");
        }
    } ///< Reject lifecycle/admission from render workers.

    void run(const frame_factory&
                 factory) noexcept;     ///< Contain failures and publish completion after cleanup.
    loop_options options;               ///< Validated immutable capacities and diagnostic deadline.
    diagnostics::services destinations; ///< Retained thread-safe optional diagnostics endpoints.
    const std::thread::id owner =
        std::this_thread::get_id(); ///< Composition root lifecycle thread.
    mutable std::mutex mutex; ///< Queue and original failure guard, never held across handlers.
    std::condition_variable
        wake; ///< Sleep an idle dedicated coordinator without consuming CPU workers.
    std::vector<frame_snapshot> queue; ///< Fixed capacity, no frame-admission allocation.
    std::size_t head = 0;              ///< Next FIFO read position.
    std::size_t tail = 0;              ///< Next FIFO write position.
    std::uint64_t last_id = 0;         ///< Last accepted identity, unaffected by backpressure.
    loop_statistics counts;            ///< Protected observational counters.
    bool stopping = false;             ///< Admission closed, accepted frames still drain.
    std::exception_ptr failure;        ///< Original initialization/render/shutdown failure.
    std::atomic<bool> done{false};     ///< Release publication after handler destruction.
    std::jthread thread;               ///< Destroyed/joined before shared execution state.
};
} // namespace atlas::render_host
