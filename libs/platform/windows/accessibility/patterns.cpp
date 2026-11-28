/** @file @brief Selection, toggle and read-only numeric patterns for menu semantics. */
#include "provider.hpp"
#include <wrl/client.h>

namespace atlas::accessibility_host::detail
{
/// @brief Selection is a bounded array of retained child interfaces, not live UI pointers.
HRESULT provider::GetSelection(SAFEARRAY** output)
{
    if (!output)
    {
        return E_POINTER;
    }

    *output = nullptr;
    const auto data = read();
    if (!data || (id_.generation && !data->find(id_)))
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }

    std::vector<accessibility::identity> selected;
    try
    {
        for (const auto& value : data->nodes())
        {
            if (value.parent == id_ && value.selected && value.select)
            {
                selected.push_back(value.id);
            }
        }

        SAFEARRAY* result =
            SafeArrayCreateVector(VT_UNKNOWN, 0, static_cast<ULONG>(selected.size()));
        if (!result)
        {
            return E_OUTOFMEMORY;
        }

        std::unique_ptr<SAFEARRAY, decltype(&SafeArrayDestroy)> guard{result, SafeArrayDestroy};
        for (LONG i = 0; i < static_cast<LONG>(selected.size()); ++i)
        {
            Microsoft::WRL::ComPtr<IRawElementProviderSimple> child;
            child.Attach(new provider{endpoint_, selected[static_cast<std::size_t>(i)]});
            const auto status = SafeArrayPutElement(result, &i, child.Get());
            if (FAILED(status))
            {
                return status;
            }
        }

        *output = guard.release();
        return S_OK;
    }
    catch (...)
    {
        return E_OUTOFMEMORY;
    }
}

/// @brief This increment exposes only single-selection lists/tab groups.
HRESULT provider::get_CanSelectMultiple(BOOL* output)
{
    if (!output)
    {
        return E_POINTER;
    }

    *output = FALSE;
    return read() ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
}

/// @brief Owner publications permit empty selection, including transient tab rebuilds.
HRESULT provider::get_IsSelectionRequired(BOOL* output)
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

    return S_OK;
}

HRESULT provider::Toggle()
{
    return enqueue(accessibility::action::invoke);
} ///< Owner-authored toggle.

/// @brief Toggle never mutates the published state on the calling client thread.
HRESULT provider::get_ToggleState(ToggleState* output)
{
    if (!output)
    {
        return E_POINTER;
    }

    *output = ToggleState_Off;
    const auto data = read();
    const auto* value = data ? data->find(id_) : nullptr;
    if (!value)
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }

    *output = value->checked ? ToggleState_On : ToggleState_Off;
    return S_OK;
}

HRESULT provider::SetValue(double)
{
    return UIA_E_INVALIDOPERATION;
} ///< Numeric editing is deferred.

/// @brief Current numeric value is read-only in this menu increment.
HRESULT provider::get_Value(double* output)
{
    if (!output)
    {
        return E_POINTER;
    }

    *output = 0;
    const auto data = read();
    const auto* value = data ? data->find(id_) : nullptr;
    if (!value)
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }

    *output = value->number;
    return S_OK;
}

/// @brief Numeric bounds are published and validated together with the current value.
HRESULT provider::get_Minimum(double* output)
{
    if (!output)
    {
        return E_POINTER;
    }

    *output = 0;
    const auto data = read();
    const auto* value = data ? data->find(id_) : nullptr;
    if (!value)
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }

    *output = value->minimum;
    return S_OK;
}

/// @brief Read the upper numeric bound without executing a UI callback.
HRESULT provider::get_Maximum(double* output)
{
    if (!output)
    {
        return E_POINTER;
    }

    *output = 0;
    const auto data = read();
    const auto* value = data ? data->find(id_) : nullptr;
    if (!value)
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }

    *output = value->maximum;
    return S_OK;
}

HRESULT provider::get_LargeChange(double* output)
{
    return get_SmallChange(output);
} ///< Read-only range.

/// @brief No editable step is advertised for a read-only slider.
HRESULT provider::get_SmallChange(double* output)
{
    if (!output)
    {
        return E_POINTER;
    }

    *output = 0;
    return read() ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
}
} // namespace atlas::accessibility_host::detail
