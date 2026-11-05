/**
 * @file
 * @brief Portable source/device/control events; no window or UI dependency.
 */
#pragma once
#include <array>
#include <atlas/input/key.hpp>
#include <span>
#include <thread>

namespace atlas::input
{
/// @brief Portable device families independent of SDK handles.
enum class device_kind : std::uint8_t
{
    keyboard, ///< Semantic key controls.
    pointer,  ///< Pointer buttons/position supplied by a backend.
    gamepad   ///< Generic controller buttons and axes; physical backend is separate.
}; ///< Device families, independent of SDK handles.

struct device_identity
{
    std::uint64_t id = 0; ///< Backend identity, scoped by source; reconnects need a new ID.
    device_kind kind = device_kind::keyboard;                ///< Device family.
    bool operator==(const device_identity&) const = default; ///< Compare identity and family.
}; ///< Never an OS pointer, HWND or borrowed SDK object.

struct input_value
{
    float x = 0;                                         ///< Scalar or first vector component.
    float y = 0;                                         ///< Second vector component.
    bool operator==(const input_value&) const = default; ///< Exact deterministic values.
}; ///< Finite values; normalization is an explicit modifier policy.

/// @brief Ready packet categories and explicit loss/reset notifications.
enum class event_kind : std::uint8_t
{
    control_changed,    ///< New button/axis value, including release zero.
    pointer_motion,     ///< Absolute physical client position in value.
    pointer_cancel,     ///< Cancel pointer ownership and its control states.
    pointer_leave,      ///< Hover exit, not a button release.
    focus_lost,         ///< Cancel controls from this source.
    source_reset,       ///< Overflow/source reset; cancel rather than invent release success.
    device_connected,   ///< Device lifecycle observation.
    device_disconnected ///< Cancel this source/device's controls.
}; ///< Backends enqueue values; application handlers run during explicit owner dispatch.

namespace pointer_control
{
inline constexpr std::uint32_t primary = 1;  ///< Primary button control.
inline constexpr std::uint32_t position = 2; ///< Absolute position vector.
} // namespace pointer_control

struct event
{
    event_kind kind = event_kind::control_changed; ///< Event category.
    device_identity device{};                      ///< Valid nonzero identity for device events.
    std::uint32_t control = 0; ///< Semantic control code; key enum values for keyboard.
    input_value value{};       ///< New value or pointer position.
    std::uint64_t source = 0;  ///< Zero is global/synthetic; queues assign a unique source.
    input_value position{};    ///< Optional physical pointer location accompanying a button sample.
}; ///< Value-only event suitable for deterministic replay.

struct event_batch
{
    std::array<event, 256> events{}; ///< Bounded transport with no event allocation.
    std::size_t count = 0;           ///< Initialized entries in FIFO order.
    bool overflow = false;           ///< Batch replaced with one source reset on overflow.

    /// @brief Borrow only while this fixed transport remains alive.
    std::span<const event> values() const noexcept
    {
        return {events.data(), count};
    }

    ///< Borrow while the batch remains alive and unchanged.
}; ///< No subscription or SDK lifetime crosses this value boundary.

class event_queue
{
  public:
    event_queue();                            ///< Create a unique source on its collecting owner.
    event_queue(const event_queue&) = delete; ///< One producer/drainer owner, not an MPSC queue.
    event_queue& operator=(const event_queue&) = delete; ///< Preserve source identity.
    bool
    try_push(event value) noexcept; ///< Nonwaiting owner publication; false on wrong thread/full.
    event_batch drain(); ///< Owner drain without waiting for input or allocating event storage.

    std::uint64_t source() const noexcept
    {
        return source_;
    } ///< Immutable source identity.
  private:
    const std::thread::id owner_ = std::this_thread::get_id(); ///< Immutable affinity.
    const std::uint64_t source_; ///< Never reused by another live/recreated queue.
    event_batch batch_;          ///< Fixed capacity; native callbacks cannot throw on publication.
}; ///< SDK callbacks on other threads require their own bounded ingress adapter.

class input_backend
{
  public:
    virtual ~input_backend() = default; ///< Stop native callbacks before releasing dependencies.
    /// @brief Collect currently ready events on the owner; never wait or dispatch client callbacks.
    virtual void pump(event_queue& destination) = 0;
    ///< Enqueue ready data on the owner, never wait for the next hardware sample.
}; ///< Startup/shutdown and SDK ownership belong to the concrete adapter/composition root.
} // namespace atlas::input
