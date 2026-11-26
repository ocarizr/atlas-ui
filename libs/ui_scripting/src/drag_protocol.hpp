/** @file @brief Value-only Lua drag protocol; validate before committing script demands. */
#pragma once
#include <array>
#include <atlas/mvvm/schema.hpp>
#include <atlas/ui/input_session.hpp>

namespace atlas::ui_scripting::detail
{
/// @brief Full identity is a string because Lua numbers cannot represent all 64-bit sessions.
inline std::string identity(ui::element_handle value)
{
    return std::to_string(value.session) + ":" + std::to_string(value.index) + ":" +
           std::to_string(value.generation);
}

/// @brief Preserve the original ten arguments and append typed optional drag metadata.
inline std::array<mvvm::value, 18> arguments(const ui::routed_event& event)
{
    std::array<mvvm::value, 18> result{
        static_cast<double>(event.delta_seconds),
        static_cast<std::int64_t>(event.phase),
        static_cast<double>(event.local.x),
        static_cast<double>(event.local.y),
        static_cast<std::int64_t>(event.key),
        event.action ? std::to_string(event.action->action) : std::string{},
        event.action ? static_cast<std::int64_t>(event.action->phase) : std::int64_t{-1},
        event.target_alive,
        static_cast<double>(event.scroll.x),
        static_cast<double>(event.scroll.y)};
    if (event.kind < ui::event_kind::drag_prepare)
    {
        return result;
    }

    const auto drag = event.drag.value_or(ui::drag_event{});
    result[10] = std::string{drag.payload.type()};
    result[11] = std::string{drag.payload.text()};
    result[12] = drag.payload.is_text();
    result[13] = static_cast<std::int64_t>(drag.operation);
    result[14] = identity(drag.source);
    result[15] = identity(drag.target);
    result[16] = std::to_string(drag.transfer);
    result[17] = static_cast<std::int64_t>(drag.reason);
    return result;
}

/// @brief Optional values extend the original five booleans without changing existing scripts.
inline ui::event_response response(std::span<const mvvm::value> values, ui::event_kind kind)
{
    const auto flag = [&](std::size_t index)
    {
        return index < values.size() && std::get<bool>(values[index]);
    };
    ui::event_response result{flag(0), flag(1), flag(2), flag(3), flag(4)};
    const auto text = [&](std::size_t index) -> std::string_view
    {
        return index < values.size() ? std::string_view{std::get<std::string>(values[index])}
                                     : std::string_view{};
    };
    if (!text(5).empty() || !text(6).empty())
    {
        if (kind != ui::event_kind::drag_prepare)
        {
            throw std::invalid_argument("drag offer is only valid during prepare");
        }

        result.offer = ui::drag_payload::text(text(5), text(6));
    }

    const auto number = [&](std::size_t index)
    {
        return index < values.size() ? std::get<std::int64_t>(values[index]) : 0;
    };
    if (number(7) < 0 || number(7) > 2 || number(8) < 0 || number(8) > 2 ||
        (kind != ui::event_kind::drag_query && (number(7) || number(8))))
    {
        throw std::invalid_argument("invalid Lua drag negotiation response");
    }

    result.acceptance = static_cast<ui::drag_acceptance>(number(7));
    result.operation = static_cast<ui::drag_operation>(number(8));
    return result;
}
} // namespace atlas::ui_scripting::detail
