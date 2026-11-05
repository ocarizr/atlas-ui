/** @file @brief Noexcept normalization of Win32 messages into bounded owner packets. */
#include "input_messages.hpp"

namespace atlas::window::detail
{
namespace
{
/// @brief Semantic identifiers deliberately differ from native navigation-key constants.
input_key semantic_key(WPARAM key) noexcept
{
    static constexpr std::array letters{
        input_key::a, input_key::b, input_key::c, input_key::d, input_key::e, input_key::f,
        input_key::g, input_key::h, input_key::i, input_key::j, input_key::k, input_key::l,
        input_key::m, input_key::n, input_key::o, input_key::p, input_key::q, input_key::r,
        input_key::s, input_key::t, input_key::u, input_key::v, input_key::w, input_key::x,
        input_key::y, input_key::z,
    };
    static constexpr std::array digits{
        input_key::digit_0, input_key::digit_1, input_key::digit_2, input_key::digit_3,
        input_key::digit_4, input_key::digit_5, input_key::digit_6, input_key::digit_7,
        input_key::digit_8, input_key::digit_9,
    };
    if (key >= 'A' && key <= 'Z')
    {
        return letters[key - 'A'];
    }

    if (key >= '0' && key <= '9')
    {
        return digits[key - '0'];
    }

    switch (key)
    {
        case VK_TAB:
            return input_key::tab;
        case VK_RETURN:
            return input_key::enter;
        case VK_ESCAPE:
            return input_key::escape;
        case VK_SPACE:
            return input_key::space;
        case VK_LEFT:
            return input_key::left;
        case VK_RIGHT:
            return input_key::right;
        case VK_UP:
            return input_key::up;
        case VK_DOWN:
            return input_key::down;
        case VK_HOME:
            return input_key::home;
        case VK_END:
            return input_key::end;
        case VK_BACK:
            return input_key::backspace;
        case VK_DELETE:
            return input_key::erase;
        default:
            return input_key::unknown;
    }
}
} // namespace

/// @brief OS callbacks never allocate, wait for jobs or invoke UI/Lua handlers.
bool collect_input(native_input_state& state, const native_input_message& packet) noexcept
{
    const auto [handle, message, key, data] = packet;
    auto& queue = state.buffer;
    if (message == WM_MOUSEMOVE || message == WM_LBUTTONDOWN || message == WM_LBUTTONUP)
    {
        const auto kind = message == WM_MOUSEMOVE     ? input_kind::pointer_move
                          : message == WM_LBUTTONDOWN ? input_kind::pointer_down
                                                      : input_kind::pointer_up;
        queue.push({kind, static_cast<std::int16_t>(LOWORD(data)),
                    static_cast<std::int16_t>(HIWORD(data))});
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
        const auto normalized = semantic_key(key);
        if (normalized != input_key::unknown)
        {
            queue.push({message == WM_KEYDOWN ? input_kind::key_down : input_kind::key_up, 0, 0,
                        normalized});
        }

        return true;
    }

    if (message == WM_MOUSELEAVE)
    {
        state.tracking = false;
        queue.push({input_kind::pointer_leave});
        return true;
    }

    if (message == WM_CAPTURECHANGED)
    {
        queue.push({input_kind::pointer_cancel});
    }

    if (message == WM_KILLFOCUS || message == WM_CLOSE)
    {
        queue.push({input_kind::focus_lost});
        if (GetCapture() == handle)
        {
            ReleaseCapture();
        }
    }

    return false;
}
} // namespace atlas::window::detail
