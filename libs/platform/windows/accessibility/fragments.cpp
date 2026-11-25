/** @file @brief Semantic fragment navigation and focus queries over immutable publications. */
#include "provider.hpp"

namespace atlas::accessibility_host::detail
{
/// @brief Navigation walks semantic links, including a synthetic HWND fragment root.
HRESULT provider::Navigate(NavigateDirection direction, IRawElementProviderFragment** output)
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

    accessibility::identity selected{};
    bool found = false;
    if (direction == NavigateDirection_Parent)
    {
        if (value)
        {
            selected = value->parent;
            found = true;
        }
    }
    else if (direction == NavigateDirection_FirstChild || direction == NavigateDirection_LastChild)
    {
        for (const auto& item : data->nodes())
        {
            if (item.parent != id_)
            {
                continue;
            }

            selected = item.id;
            found = true;
            if (direction == NavigateDirection_FirstChild)
            {
                break;
            }
        }
    }
    else if (direction == NavigateDirection_NextSibling ||
             direction == NavigateDirection_PreviousSibling)
    {
        bool passed = false;
        if (value)
        {
            for (const auto& item : data->nodes())
            {
                if (item.parent != value->parent)
                {
                    continue;
                }

                if (item.id == id_)
                {
                    if (direction == NavigateDirection_PreviousSibling)
                    {
                        break;
                    }

                    passed = true;
                }
                else if (direction == NavigateDirection_PreviousSibling || passed)
                {
                    selected = item.id;
                    found = true;
                    if (passed)
                    {
                        break;
                    }
                }
            }
        }
    }
    else
    {
        return E_INVALIDARG;
    }

    if (!found)
    {
        return S_OK;
    }

    try
    {
        *output = new provider{endpoint_, selected};
        return S_OK;
    }
    catch (...)
    {
        return E_OUTOFMEMORY;
    }
}

/// @brief No foreign HWND fragments are embedded in this provider.
HRESULT provider::GetEmbeddedFragmentRoots(SAFEARRAY** output)
{
    if (!output)
    {
        return E_POINTER;
    }

    *output = nullptr;
    return read() ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
}

/// @brief Fragment root shares the native disconnection token with every child.
HRESULT provider::get_FragmentRoot(IRawElementProviderFragmentRoot** output)
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

    try
    {
        *output = new provider{endpoint_};
        return S_OK;
    }
    catch (...)
    {
        return E_OUTOFMEMORY;
    }
}

/// @brief Last matching semantic entry is the deepest exposed descendant in publication order.
HRESULT provider::ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** output)
{
    if (!output)
    {
        return E_POINTER;
    }

    *output = nullptr;
    const auto data = read();
    if (!data)
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }

    accessibility::identity selected{};
    for (const auto& item : data->nodes())
    {
        const auto& box = item.bounds;
        const auto& clip = item.clip;
        const bool clipped =
            item.clipped &&
            (x < clip.x || y < clip.y || x >= clip.x + clip.width || y >= clip.y + clip.height);
        if (!clipped && !item.offscreen && x >= box.x && y >= box.y && x < box.x + box.width &&
            y < box.y + box.height)
        {
            selected = item.id;
        }
    }

    try
    {
        *output = new provider{endpoint_, selected};
        return S_OK;
    }
    catch (...)
    {
        return E_OUTOFMEMORY;
    }
}

/// @brief Focus reflects the latest committed owner publication, not request admission.
HRESULT provider::GetFocus(IRawElementProviderFragment** output)
{
    if (!output)
    {
        return E_POINTER;
    }

    *output = nullptr;
    const auto data = read();
    if (!data)
    {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }

    for (const auto& item : data->nodes())
    {
        if (!item.focused)
        {
            continue;
        }

        try
        {
            *output = new provider{endpoint_, item.id};
            return S_OK;
        }
        catch (...)
        {
            return E_OUTOFMEMORY;
        }
    }

    return S_OK;
}
} // namespace atlas::accessibility_host::detail
