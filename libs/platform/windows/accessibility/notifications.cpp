/** @file @brief UIA property notifications after immutable owner publication, outside locks. */
#include "provider.hpp"
#include <array>
#include <wrl/client.h>

namespace atlas::accessibility_host::detail
{
namespace
{
/// @brief Construct a property-change payload with explicit COM ownership.
VARIANT property(const accessibility::node& source, PROPERTYID id)
{
    VARIANT output{};
    switch (id)
    {
        case UIA_BoundingRectanglePropertyId:
        {
            auto* array = SafeArrayCreateVector(VT_R8, 0, 4);
            if (!array)
            {
                throw std::bad_alloc{};
            }

            std::array values{source.bounds.x, source.bounds.y, source.bounds.width,
                              source.bounds.height};
            for (LONG i = 0; i < 4; ++i)
            {
                SafeArrayPutElement(array, &i, &values[static_cast<std::size_t>(i)]);
            }

            output.parray = array;
            output.vt = VT_ARRAY | VT_R8;
            break;
        }
        case UIA_NamePropertyId:
            output.bstrVal = text(source.name);
            output.vt = VT_BSTR;
            break;
        case UIA_HelpTextPropertyId:
            output.bstrVal = text(source.description);
            output.vt = VT_BSTR;
            break;
        case UIA_ValueValuePropertyId:
            output.bstrVal = text(source.value);
            output.vt = VT_BSTR;
            break;
        case UIA_RangeValueValuePropertyId:
            output.vt = VT_R8;
            output.dblVal = source.number;
            break;
        case UIA_ToggleToggleStatePropertyId:
            output.vt = VT_I4;
            output.lVal = source.checked ? ToggleState_On : ToggleState_Off;
            break;
        case UIA_IsEnabledPropertyId:
            output.vt = VT_BOOL;
            output.boolVal = source.enabled ? VARIANT_TRUE : VARIANT_FALSE;
            break;
        case UIA_IsOffscreenPropertyId:
            output.vt = VT_BOOL;
            output.boolVal = source.offscreen ? VARIANT_TRUE : VARIANT_FALSE;
            break;
        case UIA_SelectionItemIsSelectedPropertyId:
            output.vt = VT_BOOL;
            output.boolVal = source.selected ? VARIANT_TRUE : VARIANT_FALSE;
            break;
        default:
            break;
    }

    return output;
}
} // namespace

/// @brief Payload cleanup happens even if allocating a later property fails.
void notify_changes(IRawElementProviderSimple* child, const accessibility::node& old,
                    const accessibility::node& value)
{
    constexpr std::array properties{UIA_BoundingRectanglePropertyId,
                                    UIA_NamePropertyId,
                                    UIA_HelpTextPropertyId,
                                    UIA_ValueValuePropertyId,
                                    UIA_IsEnabledPropertyId,
                                    UIA_IsOffscreenPropertyId,
                                    UIA_SelectionItemIsSelectedPropertyId,
                                    UIA_ToggleToggleStatePropertyId,
                                    UIA_RangeValueValuePropertyId};
    const std::array changed{old.bounds != value.bounds,
                             old.name != value.name,
                             old.description != value.description,
                             old.value != value.value,
                             old.enabled != value.enabled,
                             old.offscreen != value.offscreen,
                             old.selected != value.selected,
                             old.checked != value.checked,
                             old.number != value.number};
    for (std::size_t i = 0; i < properties.size(); ++i)
    {
        if (!changed[i])
        {
            continue;
        }

        VARIANT before{}, after{};
        try
        {
            before = property(old, properties[i]);
            after = property(value, properties[i]);
            UiaRaiseAutomationPropertyChangedEvent(child, properties[i], before, after);
        }
        catch (...)
        {
            VariantClear(&before);
            VariantClear(&after);
            throw;
        }

        VariantClear(&before);
        VariantClear(&after);
    }

    if (old.selected != value.selected && value.select)
    {
        UiaRaiseAutomationEvent(child, value.selected
                                           ? UIA_SelectionItem_ElementSelectedEventId
                                           : UIA_SelectionItem_ElementRemovedFromSelectionEventId);
    }
}
} // namespace atlas::accessibility_host::detail
