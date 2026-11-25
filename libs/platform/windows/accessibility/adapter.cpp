/** @file @brief Native window attachment and committed-state UI Automation notifications. */
#include "provider.hpp"
#include "window_backend.hpp"
#include <CommCtrl.h>
#include <algorithm>
#include <atlas/accessibility_host/adapter.hpp>
#include <map>
#include <thread>
#include <tuple>
#include <wrl/client.h>

namespace atlas::accessibility_host
{
/// @brief Owner-only subclass lifetime; client providers retain only a disconnection token.
class adapter::impl
{
  public:
    std::shared_ptr<detail::endpoint> endpoint;              ///< Thread-safe provider state.
    detail::provider* root = nullptr;                        ///< One retained COM reference.
    std::shared_ptr<window::detail::window_backend> window;  ///< Destroy on native owner only.
    std::shared_ptr<const accessibility::snapshot> previous; ///< Last notified values.
    diagnostics::services sinks; ///< Optional setup/shutdown and profiling destinations.
    std::thread::id owner = std::this_thread::get_id(); ///< Native affinity.

    void check() const
    {
        if (owner != std::this_thread::get_id())
        {
            throw std::logic_error("accessibility adapter requires its window owner");
        }
    } ///< Do not dispatch native lifecycle from CPU jobs.

    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters) -- Required SUBCLASSPROC ABI.
    static LRESULT CALLBACK procedure(HWND handle, UINT message, WPARAM wparam, LPARAM lparam,
                                      UINT_PTR subclass, DWORD_PTR state) noexcept
    {
        // NOLINTNEXTLINE(performance-no-int-to-ptr) -- Win32 returns our exact DWORD_PTR context.
        auto& self = *reinterpret_cast<impl*>(state);
        if (message == WM_GETOBJECT && static_cast<LONG>(lparam) == UiaRootObjectId &&
            self.endpoint->connected.load())
        {
            return UiaReturnRawElementProvider(handle, wparam, lparam, self.root);
        }

        if (message == WM_NCDESTROY)
        {
            self.endpoint->connected.store(false);
            RemoveWindowSubclass(handle, procedure, subclass);
        }

        return DefSubclassProc(handle, message, wparam, lparam);
    } ///< Never invoke application/Lua handlers across the OS callback boundary.
};

/// @brief The supported native adapter is selected entirely by CMake platform conditions.
bool available() noexcept
{
    return true;
}

/// @brief Subclass only an owner-validated window; unwind COM references on attachment failure.
adapter::adapter(window::window& target, std::shared_ptr<accessibility::channel> source,
                 diagnostics::services sinks)
    : impl_(std::make_unique<impl>())
{
    if (!source)
    {
        throw std::invalid_argument("accessibility requires a channel");
    }

    impl_->window = window::detail::window_access::retain(target);
    impl_->sinks = std::move(sinks);
    impl_->endpoint = std::make_shared<detail::endpoint>();
    impl_->endpoint->window = static_cast<HWND>(impl_->window->native_handle());
    impl_->endpoint->source = std::move(source);
    impl_->root = new detail::provider{impl_->endpoint};
    if (!SetWindowSubclass(impl_->endpoint->window, impl::procedure,
                           reinterpret_cast<UINT_PTR>(impl_.get()),
                           reinterpret_cast<DWORD_PTR>(impl_.get())))
    {
        impl_->root->Release();
        throw std::runtime_error("cannot attach Windows accessibility provider");
    }

    diagnostics::write(impl_->sinks, diagnostics::log_level::info, "accessibility.host",
                       "Windows UI Automation adapter attached");
}

/// @brief Disconnect before releasing the native owner lease; cached providers become unavailable.
adapter::~adapter()
{
    if (impl_->owner != std::this_thread::get_id())
    {
        std::terminate();
    }

    impl_->endpoint->connected.store(false);
    RemoveWindowSubclass(impl_->endpoint->window, impl::procedure,
                         reinterpret_cast<UINT_PTR>(impl_.get()));
    UiaDisconnectProvider(impl_->root);
    impl_->root->Release();
    diagnostics::write(impl_->sinks, diagnostics::log_level::info, "accessibility.host",
                       "Windows UI Automation adapter detached");
}

/// @brief Client origin must refresh when moving a window, not only on resize.
screen_transform adapter::coordinates() const
{
    impl_->check();
    POINT point{};
    if (!impl_->endpoint->connected.load() || !ClientToScreen(impl_->endpoint->window, &point))
    {
        throw std::runtime_error("accessibility window unavailable");
    }

    return {static_cast<double>(point.x), static_cast<double>(point.y),
            static_cast<double>(GetDpiForWindow(impl_->endpoint->window)) / 96.0};
}

/// @brief Structural invalidation lets clients rebuild; focus events use exact new identities.
void adapter::notify()
{
    impl_->check();
    const diagnostics::operation_scope trace{impl_->sinks,
                                             {"accessibility.host.notify", "ui.dispatch"}};
    const auto current = impl_->endpoint->source->read();
    if (current == impl_->previous)
    {
        return;
    }

    const auto prior = impl_->previous;
    impl_->previous = current;
    if (!current || !impl_->endpoint->connected.load())
    {
        return;
    }

    const auto key = [](accessibility::identity id)
    {
        return std::tuple{id.session, id.index, id.generation};
    };
    std::map<decltype(key({})), const accessibility::node*> previous;
    if (prior)
    {
        for (const auto& value : prior->nodes())
        {
            previous.emplace(key(value.id), &value);
        }
    }

    bool structural = !prior || prior->nodes().size() != current->nodes().size();
    for (const auto& value : current->nodes())
    {
        const auto found = previous.find(key(value.id));
        const auto* old = found == previous.end() ? nullptr : found->second;
        structural |= !old || old->parent != value.parent || old->kind != value.kind ||
                      old->bounds != value.bounds;
        if (old && *old == value)
        {
            continue;
        }

        Microsoft::WRL::ComPtr<detail::provider> child;
        child.Attach(new detail::provider{impl_->endpoint, value.id});
        if (old)
        {
            detail::notify_changes(child.Get(), *old, value);
        }

        if (value.focused && (!old || !old->focused))
        {
            UiaRaiseAutomationEvent(child.Get(), UIA_AutomationFocusChangedEventId);
        }
    }

    if (structural)
    {
        UiaRaiseStructureChangedEvent(impl_->root, StructureChangeType_ChildrenInvalidated, nullptr,
                                      0);
    }
}
} // namespace atlas::accessibility_host
