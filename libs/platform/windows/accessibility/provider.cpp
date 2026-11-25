/** @file @brief COM identity, supported patterns and asynchronous control actions. */
#include "provider.hpp"

namespace atlas::accessibility_host::detail
{
/// @brief Native clients may outlive the owner attachment without retaining its HWND lifetime.
provider::provider(std::shared_ptr<endpoint> source, accessibility::identity id)
    : endpoint_(std::move(source)), id_(id)
{
}

/// @brief All exposed interfaces share the simple-provider IUnknown identity.
HRESULT provider::QueryInterface(REFIID id, void** output)
{
    if (!output)
    {
        return E_POINTER;
    }

    *output = nullptr;
    if (id == __uuidof(IUnknown) || id == __uuidof(IRawElementProviderSimple))
    {
        *output = static_cast<IRawElementProviderSimple*>(this);
    }
    else if (id == __uuidof(IRawElementProviderFragment))
    {
        *output = static_cast<IRawElementProviderFragment*>(this);
    }
    else if (id == __uuidof(IRawElementProviderFragmentRoot) && !id_.generation)
    {
        *output = static_cast<IRawElementProviderFragmentRoot*>(this);
    }
    else if (id == __uuidof(IInvokeProvider))
    {
        *output = static_cast<IInvokeProvider*>(this);
    }
    else if (id == __uuidof(ISelectionItemProvider))
    {
        *output = static_cast<ISelectionItemProvider*>(this);
    }
    else if (id == __uuidof(IScrollItemProvider))
    {
        *output = static_cast<IScrollItemProvider*>(this);
    }
    else if (id == __uuidof(ISelectionProvider))
    {
        *output = static_cast<ISelectionProvider*>(this);
    }
    else if (id == __uuidof(IToggleProvider))
    {
        *output = static_cast<IToggleProvider*>(this);
    }
    else if (id == __uuidof(IRangeValueProvider))
    {
        *output = static_cast<IRangeValueProvider*>(this);
    }
    else if (id == __uuidof(IValueProvider))
    {
        *output = static_cast<IValueProvider*>(this);
    }

    if (!*output)
    {
        return E_NOINTERFACE;
    }

    AddRef();
    return S_OK;
}

/// @brief Provider references are independent of the mutable UI lifetime.
ULONG provider::AddRef()
{
    return ++references_;
}

/// @brief Final release may occur on a client thread; owned values are thread-safe.
ULONG provider::Release()
{
    const auto remaining = --references_;
    if (!remaining)
    {
        delete this;
    }

    return remaining;
}

/// @brief Disconnect makes all old providers unavailable, including cached node interfaces.
std::shared_ptr<const accessibility::snapshot> provider::read() const
{
    return endpoint_->connected.load() ? endpoint_->source->read() : nullptr;
}

/// @brief The provider implements its own thread-safe snapshot access.
HRESULT provider::get_ProviderOptions(ProviderOptions* output)
{
    if (!output)
    {
        return E_POINTER;
    }

    *output = ProviderOptions_ServerSideProvider;
    return S_OK;
}

/// @brief Advertise only operations explicitly supported by the current owner descriptor.
HRESULT provider::GetPatternProvider(PATTERNID id, IUnknown** output)
{
    if (!output)
    {
        return E_POINTER;
    }

    *output = nullptr;
    const auto data = read();
    const auto* value = data ? data->find(id_) : nullptr;
    if (!data || (id_.generation && !value))
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }

    if (!value)
    {
        return S_OK;
    }

    if (id == UIA_InvokePatternId && value->invoke && value->kind != accessibility::role::tab &&
        value->kind != accessibility::role::checkbox)
    {
        *output = static_cast<IInvokeProvider*>(this);
    }
    else if (id == UIA_SelectionItemPatternId && value->select)
    {
        *output = static_cast<ISelectionItemProvider*>(this);
    }
    else if (id == UIA_ScrollItemPatternId && value->reveal)
    {
        *output = static_cast<IScrollItemProvider*>(this);
    }
    else if (id == UIA_SelectionPatternId && (value->kind == accessibility::role::tab_group ||
                                              value->kind == accessibility::role::list))
    {
        *output = static_cast<ISelectionProvider*>(this);
    }
    else if (id == UIA_TogglePatternId && value->kind == accessibility::role::checkbox &&
             value->invoke)
    {
        *output = static_cast<IToggleProvider*>(this);
    }
    else if (id == UIA_RangeValuePatternId && value->kind == accessibility::role::slider)
    {
        *output = static_cast<IRangeValueProvider*>(this);
    }
    else if (id == UIA_ValuePatternId && !value->value.empty())
    {
        *output = static_cast<IValueProvider*>(this);
    }

    if (*output)
    {
        AddRef();
    }

    return S_OK;
}

/// @brief UIA obtains HWND host services only for the fragment root.
HRESULT provider::get_HostRawElementProvider(IRawElementProviderSimple** output)
{
    if (!output)
    {
        return E_POINTER;
    }

    *output = nullptr;
    if (!read())
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }

    return id_.generation ? S_OK : UiaHostProviderFromHwnd(endpoint_->window, output);
}

/// @brief No locks are held while the owner later runs an admitted request.
HRESULT provider::enqueue(accessibility::action operation)
{
    const auto data = read();
    const auto* value = data ? data->find(id_) : nullptr;
    if (!value)
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }

    if (!value->enabled)
    {
        return UIA_E_ELEMENTNOTENABLED;
    }

    return endpoint_->source->request({id_, operation}) ? S_OK : UIA_E_INVALIDOPERATION;
}

HRESULT provider::SetFocus()
{
    return enqueue(accessibility::action::focus);
} ///< Owner focus only.

HRESULT provider::Invoke()
{
    return enqueue(accessibility::action::invoke);
} ///< Existing activation.

HRESULT provider::Select()
{
    return enqueue(accessibility::action::select);
} ///< Authored selection.

HRESULT provider::AddToSelection()
{
    return Select();
} ///< Single selection request.

HRESULT provider::RemoveFromSelection()
{
    return UIA_E_INVALIDOPERATION;
} ///< No implicit deselection.

HRESULT provider::ScrollIntoView()
{
    return enqueue(accessibility::action::reveal);
} ///< Existing reveal.

HRESULT provider::SetValue(LPCWSTR)
{
    return UIA_E_INVALIDOPERATION;
} ///< Editing is outside this adapter.

/// @brief Read-only reporting is consistent with unsupported SetValue.
HRESULT provider::get_IsReadOnly(BOOL* output)
{
    if (!output)
    {
        return E_POINTER;
    }

    *output = TRUE;
    return read() ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
}

/// @brief Return an owned BSTR without holding mutable UI pointers.
HRESULT provider::get_Value(BSTR* output)
{
    if (!output)
    {
        return E_POINTER;
    }

    *output = nullptr;
    const auto data = read();
    const auto* value = data ? data->find(id_) : nullptr;
    if (!value)
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }

    try
    {
        *output = text(value->value);
        return S_OK;
    }
    catch (...)
    {
        return E_OUTOFMEMORY;
    }
}

/// @brief Selected state never follows keyboard focus implicitly.
HRESULT provider::get_IsSelected(BOOL* output)
{
    if (!output)
    {
        return E_POINTER;
    }

    *output = FALSE;
    const auto data = read();
    const auto* value = data ? data->find(id_) : nullptr;
    if (!value)
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }

    *output = value->selected;
    return S_OK;
}

/// @brief A selection item must have an authored semantic container.
HRESULT provider::get_SelectionContainer(IRawElementProviderSimple** output)
{
    if (!output)
    {
        return E_POINTER;
    }

    *output = nullptr;
    const auto data = read();
    const auto* value = data ? data->find(id_) : nullptr;
    if (!value)
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }

    try
    {
        *output = new provider{endpoint_, value->parent};
        return S_OK;
    }
    catch (...)
    {
        return E_OUTOFMEMORY;
    }
}
} // namespace atlas::accessibility_host::detail
