/** @file @brief Application mapping from portable native packets to UI replay values. */
#pragma once
#include <atlas/ui/input_session.hpp>
#include <atlas/window/window.hpp>

namespace atlas::application
{
/// @brief Fixed application-side normalization scratch.
struct ui_input_batch
{
    std::array<ui::input_event, 256>
        events{};          ///< Bounded stack transport matching native capacity.
    std::size_t count = 0; ///< Converted event count.

    /// @brief Borrow normalized events during the current main-loop iteration.
    std::span<const ui::input_event> values() const
    {
        return {events.data(), count};
    }

    ///< Borrow only while the transport value remains alive.
};

/// @brief The demonstration uses one logical unit per physical pixel for both viewport and input.
inline ui_input_batch normalize_input(const window::input_batch& source)
{
    ui_input_batch result;
    for (const auto& event : source.values())
    {
        ui::input_kind kind{};
        switch (event.kind)
        {
            case window::input_kind::pointer_move:
                kind = ui::input_kind::pointer_move;
                break;
            case window::input_kind::pointer_down:
                kind = ui::input_kind::pointer_down;
                break;
            case window::input_kind::pointer_up:
                kind = ui::input_kind::pointer_up;
                break;
            case window::input_kind::pointer_cancel:
                kind = ui::input_kind::pointer_cancel;
                break;
            case window::input_kind::pointer_leave:
                kind = ui::input_kind::pointer_leave;
                break;
            case window::input_kind::key_down:
                kind = ui::input_kind::key_down;
                break;
            case window::input_kind::key_up:
                kind = ui::input_kind::key_up;
                break;
            case window::input_kind::focus_lost:
                kind = ui::input_kind::window_blur;
                break;
        }

        result.events[result.count++] = {kind,
                                         {static_cast<float>(event.x), static_cast<float>(event.y)},
                                         static_cast<std::uint32_t>(event.key)};
    }

    return result;
}
} // namespace atlas::application
