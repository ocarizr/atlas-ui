/** @file @brief Win32 window implementation with deferred native destruction. */
#include "window_backend.hpp"
#include <Windows.h>
#include <mutex>
#include <system_error>

namespace atlas::window
{
namespace
{
/// @brief Translate a Win32 failure into an exception outside message dispatch.
void check(bool success)
{
    if (!success)
    {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                "window operation");
    }
}

/// @brief Stable WNDPROC state retained by presentation surfaces.
class native_window final : public detail::window_backend
{
  public:
    /// @brief Create a UTF-8-titled native window and establish physical client dimensions.
    explicit native_window(const window_options& options);

    /// @brief Release retained resources on the owner thread after outstanding work completes.
    ~native_window() override
    {
        if (handle_ != nullptr)
        {
            DestroyWindow(handle_);
        }
    }

    /// @brief Dispatch pending native messages without blocking the owner thread.
    window_state poll() override;

    /// @brief Return the current physical client state on the owner thread.
    window_state state() const override
    {
        return state_;
    }

    /// @brief Resize the physical client area without changing its screen position.
    void resize(std::uint32_t width, std::uint32_t height) override;

    /// @brief Hide the native window and mark it closed without immediate destruction.
    void close() override
    {
        state_.close_requested = true;
        ShowWindow(handle_, SW_HIDE);
    }

    /// @brief Wait on the coordinator with a diagnostic deadline; CPU workers must not block here.
    void wait(std::uint32_t milliseconds) override
    {
        MsgWaitForMultipleObjectsEx(0, nullptr, milliseconds, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }

    /// @brief Expose the opaque handle only inside the private native adapter boundary.
    void* native_handle() const override
    {
        return handle_;
    }

  private:
    /// @brief Update native window state without allowing exceptions across the OS callback
    /// boundary.
    static LRESULT CALLBACK procedure(HWND handle, UINT message, WPARAM wparam,
                                      LPARAM lparam) noexcept;
    HWND handle_ = nullptr; ///< Native handle, destroyed after the last surface releases it.
    window_state state_;    ///< Owner-thread snapshot.
};

/// @brief Register the native window class and create a physical client with a validated UTF-8
/// title.
native_window::native_window(const window_options& options)
{
    static std::once_flag registration;
    std::call_once(registration,
                   []
                   {
                       WNDCLASSEXW description{};
                       description.cbSize = sizeof(description);
                       description.lpfnWndProc = procedure;
                       description.hInstance = GetModuleHandleW(nullptr);
                       description.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
                       description.lpszClassName = L"atlas_presentation_window";
                       check(RegisterClassExW(&description) != 0);
                   });
    const int length =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, options.title.c_str(), -1, nullptr, 0);
    check(length > 0);
    std::wstring title(static_cast<std::size_t>(length), L'\0');
    check(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, options.title.c_str(), -1,
                              title.data(), length) > 0);
    RECT rectangle{0, 0, static_cast<LONG>(options.width), static_cast<LONG>(options.height)};
    check(AdjustWindowRectExForDpi(&rectangle, WS_OVERLAPPEDWINDOW, FALSE, 0, GetDpiForSystem()) !=
          0);
    handle_ = CreateWindowExW(0, L"atlas_presentation_window", title.c_str(), WS_OVERLAPPEDWINDOW,
                              CW_USEDEFAULT, CW_USEDEFAULT, rectangle.right - rectangle.left,
                              rectangle.bottom - rectangle.top, nullptr, nullptr,
                              GetModuleHandleW(nullptr), this);
    if (handle_ == nullptr)
    {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                "create native window");
    }

    try
    {
        state_.dpi_scale = static_cast<float>(GetDpiForWindow(handle_)) / 96.0F;
        resize(options.width, options.height);
        if (options.visible)
        {
            ShowWindow(handle_, SW_SHOW);
        }
    }
    catch (...)
    {
        DestroyWindow(handle_);
        throw;
    }
}

/// @brief Dispatch pending native messages without blocking the owner thread.
window_state native_window::poll()
{
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return state_;
}

/// @brief Throw an operation-specific diagnostic before continuing after a native failure.
void native_window::resize(std::uint32_t width, std::uint32_t height)
{
    RECT rectangle{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    check(AdjustWindowRectExForDpi(&rectangle, WS_OVERLAPPEDWINDOW, FALSE, 0,
                                   GetDpiForWindow(handle_)) != 0);
    check(SetWindowPos(handle_, nullptr, 0, 0, rectangle.right - rectangle.left,
                       rectangle.bottom - rectangle.top,
                       SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != 0);
    RECT client{};
    check(GetClientRect(handle_, &client) != 0);
    state_.width = static_cast<std::uint32_t>(client.right - client.left);
    state_.height = static_cast<std::uint32_t>(client.bottom - client.top);
}

/// @brief Update native window state without allowing exceptions across the OS callback boundary.
LRESULT CALLBACK native_window::procedure(HWND handle, UINT message, WPARAM wparam,
                                          LPARAM lparam) noexcept
{
    // Win32 stores the caller's pointer in a LONG_PTR slot; conversion is required by the SDK ABI.
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    auto* self = reinterpret_cast<native_window*>(GetWindowLongPtrW(handle, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        // WM_NCCREATE carries CREATESTRUCTW through the integer LPARAM SDK ABI.
        // NOLINTBEGIN(performance-no-int-to-ptr)
        self =
            static_cast<native_window*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        // NOLINTEND(performance-no-int-to-ptr)
        SetWindowLongPtrW(handle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }

    if (self != nullptr)
    {
        if (message == WM_SIZE)
        {
            self->state_.width = LOWORD(lparam);
            self->state_.height = HIWORD(lparam);
            self->state_.minimized = wparam == SIZE_MINIMIZED;
            return 0;
        }

        if (message == WM_DPICHANGED)
        {
            self->state_.dpi_scale = static_cast<float>(HIWORD(wparam)) / 96.0F;
            // WM_DPICHANGED carries its suggested RECT through LPARAM.
            // NOLINTNEXTLINE(performance-no-int-to-ptr)
            const auto* bounds = reinterpret_cast<const RECT*>(lparam);
            SetWindowPos(handle, nullptr, bounds->left, bounds->top, bounds->right - bounds->left,
                         bounds->bottom - bounds->top, SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }

        if (message == WM_CLOSE)
        {
            self->state_.close_requested = true;
            ShowWindow(handle, SW_HIDE);
            return 0;
        }

        if (message == WM_NCDESTROY)
        {
            SetWindowLongPtrW(handle, GWLP_USERDATA, 0);
            self->handle_ = nullptr;
        }
    }

    return DefWindowProcW(handle, message, wparam, lparam);
}
} // namespace

/// @brief Report native backend availability without allocating graphics resources.
bool backend_available() noexcept
{
    return true;
}

/// @brief Construct the compiled platform adapter or report an unavailable backend.
std::shared_ptr<detail::window_backend> detail::make_window_backend(const window_options& options)
{
    return std::make_shared<native_window>(options);
}
} // namespace atlas::window
