/** @file @brief Noexcept normalization of Win32 messages into bounded owner packets. */
#include "win32_input.hpp"

namespace atlas::input::detail
{
namespace
{
/// @brief Semantic identifiers deliberately differ from native navigation-key constants.
key semantic_key(WPARAM native_key) noexcept
{
    static constexpr std::array letters{
        key::a, key::b, key::c, key::d, key::e, key::f, key::g, key::h, key::i,
        key::j, key::k, key::l, key::m, key::n, key::o, key::p, key::q, key::r,
        key::s, key::t, key::u, key::v, key::w, key::x, key::y, key::z,
    };
    static constexpr std::array digits{
        key::digit_0, key::digit_1, key::digit_2, key::digit_3, key::digit_4,
        key::digit_5, key::digit_6, key::digit_7, key::digit_8, key::digit_9,
    };
    if (native_key >= 'A' && native_key <= 'Z')
    {
        return letters[native_key - 'A'];
    }

    if (native_key >= '0' && native_key <= '9')
    {
        return digits[native_key - '0'];
    }

    switch (native_key)
    {
        case VK_TAB:
            return key::tab;
        case VK_RETURN:
            return key::enter;
        case VK_ESCAPE:
            return key::escape;
        case VK_SPACE:
            return key::space;
        case VK_LEFT:
            return key::left;
        case VK_RIGHT:
            return key::right;
        case VK_UP:
            return key::up;
        case VK_DOWN:
            return key::down;
        case VK_HOME:
            return key::home;
        case VK_END:
            return key::end;
        case VK_BACK:
            return key::backspace;
        case VK_DELETE:
            return key::erase;
        default:
            return key::unknown;
    }
}
} // namespace

/// @brief OS callbacks never allocate, wait for jobs or invoke UI/Lua handlers.
bool collect_input(native_input_state& state, const native_input_message& packet) noexcept
{
    const auto [handle, message, native_key, data] = packet;
    auto& queue = state.buffer;
    if (message == WM_MOUSEMOVE || message == WM_LBUTTONDOWN || message == WM_LBUTTONUP)
    {
        const input_value position{static_cast<float>(static_cast<std::int16_t>(LOWORD(data))),
                                   static_cast<float>(static_cast<std::int16_t>(HIWORD(data)))};
        const bool motion = message == WM_MOUSEMOVE;
        const auto kind = motion ? event_kind::pointer_motion : event_kind::control_changed;
        const auto control = motion ? pointer_control::position : pointer_control::primary;
        const auto value =
            motion ? position : input_value{message == WM_LBUTTONDOWN ? 1.F : 0.F, 0};
        (void)queue.try_push({kind, {2, device_kind::pointer}, control, value, 0, position});
        if (message == WM_MOUSEMOVE && !state.tracking)
        {
            TRACKMOUSEEVENT tracking{sizeof(TRACKMOUSEEVENT), TME_LEAVE, handle, 0};
            state.tracking = TrackMouseEvent(&tracking) != 0;
        }

        if (message == WM_LBUTTONDOWN)
        {
            SetCapture(handle);
        }

        if (message == WM_LBUTTONUP && GetCapture() == handle)
        {
            ReleaseCapture();
        }

        return true;
    }

    if (message == WM_KEYDOWN || message == WM_KEYUP)
    {
        const auto normalized = semantic_key(native_key);
        if (normalized != key::unknown)
        {
            (void)queue.try_push({event_kind::control_changed,
                                  {1, device_kind::keyboard},
                                  static_cast<std::uint32_t>(normalized),
                                  {message == WM_KEYDOWN ? 1.F : 0.F, 0}});
        }

        return true;
    }

    if (message == WM_MOUSELEAVE)
    {
        state.tracking = false;
        (void)queue.try_push({event_kind::pointer_leave, {2, device_kind::pointer}});
        return true;
    }

    if (message == WM_CAPTURECHANGED)
    {
        (void)queue.try_push({event_kind::pointer_cancel, {2, device_kind::pointer}});
    }

    if (message == WM_KILLFOCUS || message == WM_CLOSE)
    {
        (void)queue.try_push({event_kind::focus_lost});
        if (GetCapture() == handle)
        {
            ReleaseCapture();
        }
    }

    return false;
}
} // namespace atlas::input::detail
