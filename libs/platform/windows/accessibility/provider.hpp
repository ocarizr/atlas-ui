/** @file @brief Private COM providers read immutable values and enqueue requests only. */
#pragma once
// clang-format off
#include <Windows.h>
#include <objbase.h>
#include <UIAutomation.h>
// clang-format on
#include <atlas/accessibility/channel.hpp>
#include <atomic>

namespace atlas::accessibility_host::detail
{
/// @brief Native lifetime token does not retain the owner-affine window backend on client threads.
struct endpoint
{
    HWND window = nullptr; ///< Immutable while connected; never destroyed by providers.
    std::shared_ptr<accessibility::channel> source; ///< Thread-safe values and request queue.
    std::atomic<bool> connected{true}; ///< False before detaching/destroying native owner.
};

/// @brief One COM identity for one generation-safe semantic node, or the native fragment root.
class provider final : public IRawElementProviderSimple,
                       public IRawElementProviderFragment,
                       public IRawElementProviderFragmentRoot,
                       public IInvokeProvider,
                       public ISelectionItemProvider,
                       public IScrollItemProvider,
                       public ISelectionProvider,
                       public IToggleProvider,
                       public IRangeValueProvider,
                       public IValueProvider
{
  public:
    provider(std::shared_ptr<endpoint> source,
             accessibility::identity id = {}); ///< Retain endpoint.
    /// @brief COM identity.
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** output) override;
    /// @brief Atomic client retain.
    ULONG STDMETHODCALLTYPE AddRef() override;
    /// @brief Delete only after final client release.
    ULONG STDMETHODCALLTYPE Release() override;
    /// @brief Server side.
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* output) override;
    /// @brief Capabilities.
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID id, IUnknown** output) override;
    /// @brief Current values.
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID id, VARIANT* output) override;
    /// @brief HWND root.
    HRESULT STDMETHODCALLTYPE
    get_HostRawElementProvider(IRawElementProviderSimple** output) override;
    /// @brief Semantic hierarchy.
    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction,
                                       IRawElementProviderFragment** output) override;
    /// @brief Stable complete identity.
    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** output) override;
    /// @brief Physical screen bounds.
    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* output) override;
    /// @brief No embedded hosts.
    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** output) override;
    /// @brief Enqueue owner focus request.
    HRESULT STDMETHODCALLTYPE SetFocus() override;
    /// @brief Root provider.
    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** output) override;
    /// @brief Presented hit.
    HRESULT STDMETHODCALLTYPE
    ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** output) override;
    /// @brief Actual focus.
    HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** output) override;
    /// @brief Enqueue existing semantic activation.
    HRESULT STDMETHODCALLTYPE Invoke() override;
    /// @brief Enqueue authored selection.
    HRESULT STDMETHODCALLTYPE Select() override;
    /// @brief Single-selection only.
    HRESULT STDMETHODCALLTYPE AddToSelection() override;
    /// @brief Unsupported deselect operation.
    HRESULT STDMETHODCALLTYPE RemoveFromSelection() override;
    /// @brief Current owner selection.
    HRESULT STDMETHODCALLTYPE get_IsSelected(BOOL* output) override;
    /// @brief Semantic parent.
    HRESULT STDMETHODCALLTYPE get_SelectionContainer(IRawElementProviderSimple** output) override;
    /// @brief Enqueue existing reveal contract.
    HRESULT STDMETHODCALLTYPE ScrollIntoView() override;
    /// @brief No text editing is advertised.
    HRESULT STDMETHODCALLTYPE SetValue(LPCWSTR value) override;
    /// @brief Read-only accessible value.
    HRESULT STDMETHODCALLTYPE get_Value(BSTR* output) override;
    /// @brief Always true.
    HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL* output) override;
    /// @brief Selected child interfaces.
    HRESULT STDMETHODCALLTYPE GetSelection(SAFEARRAY** output) override;
    /// @brief Single selection only.
    HRESULT STDMETHODCALLTYPE get_CanSelectMultiple(BOOL* output) override;
    /// @brief Required for nonempty tabs.
    HRESULT STDMETHODCALLTYPE get_IsSelectionRequired(BOOL* output) override;
    /// @brief Existing semantic toggle activation.
    HRESULT STDMETHODCALLTYPE Toggle() override;
    /// @brief Current checked state.
    HRESULT STDMETHODCALLTYPE get_ToggleState(ToggleState* output) override;
    /// @brief Numeric editing is not advertised.
    HRESULT STDMETHODCALLTYPE SetValue(double value) override;
    /// @brief Read-only numeric value.
    HRESULT STDMETHODCALLTYPE get_Value(double* output) override;
    /// @brief Numeric minimum.
    HRESULT STDMETHODCALLTYPE get_Minimum(double* output) override;
    /// @brief Numeric maximum.
    HRESULT STDMETHODCALLTYPE get_Maximum(double* output) override;
    /// @brief No editable increments.
    HRESULT STDMETHODCALLTYPE get_LargeChange(double* output) override;
    /// @brief No editable increments.
    HRESULT STDMETHODCALLTYPE get_SmallChange(double* output) override;
    std::shared_ptr<const accessibility::snapshot>
    read() const; ///< Null after native disconnection.
  private:
    HRESULT enqueue(accessibility::action operation); ///< No callbacks on client threads.
    std::atomic<ULONG> references_{1};                ///< Independent COM lifetime.
    std::shared_ptr<endpoint> endpoint_; ///< Shared disconnection and snapshot channel.
    accessibility::identity id_;         ///< Never index-only identity.
};

void notify_changes(IRawElementProviderSimple* child, const accessibility::node& old,
                    const accessibility::node& value); ///< Committed property notifications.
BSTR text(
    std::string_view utf8); ///< Validated UTF-8 to owned COM string; throws on allocation failure.
} // namespace atlas::accessibility_host::detail
