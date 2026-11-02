/** @file @brief Synchronized snapshots and an immutable native handle for presentation leases. */
#pragma once
#include "window_backend.hpp"
#include <mutex>

namespace atlas::window::detail
{
/// @brief Main retains native ownership; render consumers only read this published endpoint.
class presentation_endpoint final : public window_backend
{
  public:
    explicit presentation_endpoint(std::shared_ptr<window_backend> window)
        : window_(std::move(window)), handle_(window_->native_handle()), state_(window_->state())
    {
    } ///< Snapshot the opaque handle on the native owner thread.

    void publish(window_state state)
    {
        const std::lock_guard lock{mutex_};
        state_ = state;
    } ///< Publish after owner-thread message processing, resize or close.

    window_state state() const override
    {
        const std::lock_guard lock{mutex_};
        return state_;
    } ///< Never read mutable native window fields on the render thread.

    void* native_handle() const override
    {
        return handle_;
    } ///< Read the immutable cached handle.

    window_state poll() override
    {
        throw std::logic_error("presentation lease cannot pump messages");
    } ///< Owner-only operation.

    void resize(std::uint32_t, std::uint32_t) override
    {
        throw std::logic_error("presentation lease cannot resize its window");
    } ///< Owner-only operation.

    void close() override
    {
        throw std::logic_error("presentation lease cannot close its window");
    } ///< Owner-only operation.

    void wait(std::uint32_t) override
    {
        throw std::logic_error("presentation lease cannot wait on native messages");
    } ///< Owner-only operation.
  private:
    std::shared_ptr<window_backend> window_; ///< Main must outlive render lease release.
    void* const handle_;                     ///< No SDK type crosses the public boundary.
    mutable std::mutex mutex_; ///< Protect only snapshots, never DXGI calls or the message pump.
    window_state state_;       ///< Value-only publication from the window owner.
};
} // namespace atlas::window::detail
