/**
 * @file
 * @brief Application-owned render coordination with bounded immutable frame handoff.
 */
#pragma once
#include <atlas/diagnostics/instrumentation.hpp>
#include <atlas/window/window.hpp>
#include <functional>

namespace atlas::render_host
{
/// @brief Type-erased immutable CPU packet; destruction must be safe on either handoff thread.
class frame_payload
{
  public:
    virtual ~frame_payload() = default; ///< Never own thread-affine graphics/window objects here.
};

/// @brief Main-thread value snapshot; consumers never access mutable application state implicitly.
struct frame_snapshot
{
    std::uint64_t id = 0;        ///< Positive increasing identity assigned before admission.
    window::window_state window; ///< Published dimensions, visibility and close state.
    std::shared_ptr<const frame_payload>
        content{}; ///< Optional immutable UI/world packet, retained through consumption.
};

/// @brief Render-thread object created, called and destroyed on that same coordinator.
class frame_handler
{
  public:
    virtual ~frame_handler() = default; ///< Release graphics before the native window owner exits.
    virtual void
    render(const frame_snapshot& frame) = 0; ///< Consume one immutable snapshot in FIFO order.

    virtual void shutdown()
    {
    } ///< Drain/validate graphics before handler destruction; may report failure.
};

using frame_factory =
    std::function<std::unique_ptr<frame_handler>()>; ///< Runs on the render thread.

/// @brief Explicit frame lead and shutdown diagnostics; executing work is additional to queue
/// capacity.
struct loop_options
{
    std::size_t capacity = 2; ///< At most this many queued snapshots, plus one executing.
    std::uint32_t drain_timeout_ms =
        15000; ///< Diagnostic deadline while the owner continues pumping.
};

/// @brief Thread-safe observational handoff counters.
struct loop_statistics
{
    std::uint64_t accepted = 0;  ///< Successfully admitted identities.
    std::uint64_t completed = 0; ///< Successfully processed snapshots.
    std::uint64_t failed = 0;    ///< Accepted snapshots whose render body failed.
    std::uint64_t discarded = 0; ///< Queued snapshots discarded after a render failure.
    std::size_t queued = 0;      ///< Currently queued snapshots.
    std::size_t peak_queued = 0; ///< Highest observed pending count.
};

/// @brief Composition-owned persistent thread; no scheduler pool or global render singleton.
class render_loop
{
  public:
    explicit render_loop(
        frame_factory factory, loop_options options = {},
        diagnostics::services destinations =
            {});    ///< Validate limits before starting a persistent dedicated coordinator.
    ~render_loop(); ///< Stop/join fallback; DXGI consumers must first stop_and_join with their
                    ///< message pump.
    render_loop(const render_loop&) = delete;            ///< One main-thread lifecycle owner.
    render_loop& operator=(const render_loop&) = delete; ///< No implicit thread ownership transfer.
    bool try_submit(
        frame_snapshot frame); ///< Nonblocking backpressure; never hold a lock across rendering.
    void request_stop();       ///< Close admission and drain already accepted frames.
    bool finished()
        const noexcept; ///< True only after handler/graphics destruction on the render thread.
    void stop_and_join(const std::function<void()>& pump); ///< Keep pumping until completion, join,
                                                           ///< then rethrow original failure.
    loop_statistics statistics() const; ///< Observe queue bounds and processing outcomes.
  private:
    class impl;                  ///< Exclusive thread plus shared bounded queue and failure state.
    std::unique_ptr<impl> impl_; ///< Lifetime extends through cooperative worker drain.
};
} // namespace atlas::render_host
