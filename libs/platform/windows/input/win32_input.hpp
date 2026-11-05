/** @file @brief Private Win32-to-portable primary-input adapter. */
#pragma once
#include <Windows.h>
#include <atlas/input/events.hpp>

namespace atlas::input::detail
{
struct native_input_state
{
    event_queue buffer;    ///< Portable bounded event storage.
    bool tracking = false; ///< Mouse-leave tracking has been requested.
}; ///< Only the WNDPROC/window owner accesses mutable state.

struct native_input_message
{
    HWND handle;  ///< Message recipient.
    UINT message; ///< Native message category.
    WPARAM key;   ///< Native key/button payload.
    LPARAM data;  ///< Native coordinate/repeat payload.
}; ///< Named transport avoids interchangeable integral arguments.

/// @brief Normalize without throwing or invoking application callbacks.
bool collect_input(native_input_state& state, const native_input_message& packet) noexcept;
///< Normalize input without throwing or dispatching application callbacks.
} // namespace atlas::input::detail
