/** @file @brief UI Automation semantic properties with owned COM string values. */
#include "provider.hpp"
#include <array>
#include <limits>

namespace atlas::accessibility_host::detail
{
/// @brief Convert validated bounded text; allocation failure never crosses a COM boundary.
BSTR text(std::string_view utf8)
{
    const auto size = static_cast<int>(utf8.size());
    // NOLINTBEGIN(bugprone-suspicious-stringview-data-usage) -- Both Win32 calls have explicit byte
    // counts.
    const auto length =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), size, nullptr, 0);
    if (!utf8.empty() && !length)
    {
        throw std::invalid_argument("invalid accessibility UTF-8");
    }

    BSTR output = SysAllocStringLen(nullptr, static_cast<UINT>(length));
    if (!output)
    {
        throw std::bad_alloc{};
    }

    if (length)
    {
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), size, output, length);
    }

    // NOLINTEND(bugprone-suspicious-stringview-data-usage)
    return output;
}

/// @brief Return only truthful current properties; unsupported properties remain VT_EMPTY.
HRESULT provider::GetPropertyValue(PROPERTYID property, VARIANT* output)
{
    if (!output)
    {
        return E_POINTER;
    }

    VariantInit(output);
    const auto data = read();
    const auto* value = data ? data->find(id_) : nullptr;
    if (!data || (id_.generation && !value))
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }

    try
    {
        const auto boolean = [&](bool state)
        {
            output->vt = VT_BOOL;
            output->boolVal = state ? VARIANT_TRUE : VARIANT_FALSE;
        };
        const auto string = [&](std::string_view content)
        {
            output->bstrVal = text(content);
            output->vt = VT_BSTR;
        };
        switch (property)
        {
            case UIA_NamePropertyId:
                string(value ? value->name : "Atlas UI");
                break;
            case UIA_HelpTextPropertyId:
                if (value)
                {
                    string(value->description);
                }
                break;
            case UIA_FrameworkIdPropertyId:
                string("Atlas UI");
                break;
            case UIA_AutomationIdPropertyId:
                if (value)
                {
                    string(std::to_string(id_.session) + ":" + std::to_string(id_.index) + ":" +
                           std::to_string(id_.generation));
                }
                break;
            case UIA_ControlTypePropertyId:
            {
                constexpr std::array types{
                    UIA_GroupControlTypeId,  UIA_TextControlTypeId,     UIA_ImageControlTypeId,
                    UIA_ButtonControlTypeId, UIA_TabControlTypeId,      UIA_TabItemControlTypeId,
                    UIA_ListControlTypeId,   UIA_ListItemControlTypeId, UIA_CheckBoxControlTypeId,
                    UIA_SliderControlTypeId, UIA_PaneControlTypeId};
                output->vt = VT_I4;
                output->lVal =
                    value ? types.at(static_cast<std::size_t>(value->kind)) : UIA_PaneControlTypeId;
                break;
            }
            case UIA_IsControlElementPropertyId:
            case UIA_IsContentElementPropertyId:
                boolean(true);
                break;
            case UIA_IsEnabledPropertyId:
                boolean(!value || value->enabled);
                break;
            case UIA_IsOffscreenPropertyId:
                boolean(value && value->offscreen);
                break;
            case UIA_IsKeyboardFocusablePropertyId:
                boolean(value && value->focusable);
                break;
            case UIA_HasKeyboardFocusPropertyId:
                boolean(value && value->focused);
                break;
            case UIA_SelectionItemIsSelectedPropertyId:
                if (value && value->select)
                {
                    boolean(value->selected);
                }
                break;
            default:
                break;
        }

        return S_OK;
    }
    catch (...)
    {
        VariantClear(output);
        return E_OUTOFMEMORY;
    }
}

/// @brief Encode session, slot and generation rather than a reusable pointer/index alone.
HRESULT provider::GetRuntimeId(SAFEARRAY** output)
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

    if (!id_.generation)
    {
        return S_OK;
    }

    std::array<int, 5> parts{UiaAppendRuntimeId, static_cast<int>(id_.session >> 32),
                             static_cast<int>(id_.session & 0xFFFFFFFF),
                             static_cast<int>(id_.index), static_cast<int>(id_.generation)};
    SAFEARRAY* result = SafeArrayCreateVector(VT_I4, 0, static_cast<ULONG>(parts.size()));
    if (!result)
    {
        return E_OUTOFMEMORY;
    }

    for (LONG i = 0; i < static_cast<LONG>(parts.size()); ++i)
    {
        const auto status = SafeArrayPutElement(result, &i, &parts[static_cast<std::size_t>(i)]);
        if (FAILED(status))
        {
            SafeArrayDestroy(result);
            return status;
        }
    }

    *output = result;
    return S_OK;
}

/// @brief Child bounds are frozen physical pixels, not a worker read of the live UI.
HRESULT provider::get_BoundingRectangle(UiaRect* output)
{
    if (!output)
    {
        return E_POINTER;
    }

    *output = {};
    const auto data = read();
    if (!data)
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }

    if (id_.generation)
    {
        const auto* value = data->find(id_);
        if (!value)
        {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }

        *output = {value->bounds.x, value->bounds.y, value->bounds.width, value->bounds.height};
    }
    else
    {
        RECT area{};
        if (!GetWindowRect(endpoint_->window, &area))
        {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }

        *output = {static_cast<double>(area.left), static_cast<double>(area.top),
                   static_cast<double>(area.right - area.left),
                   static_cast<double>(area.bottom - area.top)};
    }

    return S_OK;
}
} // namespace atlas::accessibility_host::detail
