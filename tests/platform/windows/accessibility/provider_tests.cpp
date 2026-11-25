/** @file @brief Native provider fixtures validate properties, actions, recycling and disconnect. */
#include "provider_fixture.hpp"
#include <algorithm>
#include <atlas/accessibility_host/adapter.hpp>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>
#include <wrl/client.h>

namespace atlas::accessibility_host::tests
{
/// @brief Verify exposes names and advertises only supported patterns.
TEST_F(native_accessibility_test, ExposesNamesAndAdvertisesOnlySupportedPatterns)
{
    Microsoft::WRL::ComPtr<detail::provider> child;
    child.Attach(new detail::provider{endpoint, button.id});
    VARIANT name{};
    ASSERT_EQ(child->GetPropertyValue(UIA_NamePropertyId, &name), S_OK);
    EXPECT_EQ(name.vt, VT_BSTR);
    EXPECT_STREQ(name.bstrVal, L"Resume");
    VariantClear(&name);
    Microsoft::WRL::ComPtr<IUnknown> action;
    EXPECT_EQ(child->GetPatternProvider(UIA_InvokePatternId, &action), S_OK);
    EXPECT_NE(action.Get(), nullptr);
    action.Reset();
    EXPECT_EQ(child->GetPatternProvider(UIA_TextPatternId, &action), S_OK);
    EXPECT_EQ(action.Get(), nullptr);
}

/// @brief Verify native invoke queues and old providers cannot hit recycled controls.
TEST_F(native_accessibility_test, NativeInvokeQueuesAndOldProvidersCannotHitRecycledControls)
{
    Microsoft::WRL::ComPtr<detail::provider> child;
    child.Attach(new detail::provider{endpoint, button.id});
    EXPECT_EQ(child->Invoke(), S_OK);
    EXPECT_EQ(channel->take_requests().size(), 1);
    ++button.id.generation;
    channel->publish({button});
    EXPECT_EQ(child->Invoke(), UIA_E_ELEMENTNOTAVAILABLE);
    EXPECT_TRUE(channel->take_requests().empty());
}

/// @brief Verify retained providers disconnect and reject null outputs.
TEST_F(native_accessibility_test, RetainedProvidersDisconnectAndRejectNullOutputs)
{
    Microsoft::WRL::ComPtr<detail::provider> child;
    child.Attach(new detail::provider{endpoint, button.id});
    EXPECT_EQ(child->GetPropertyValue(UIA_NamePropertyId, nullptr), E_POINTER);
    endpoint->connected.store(false);
    UiaRect area{};
    EXPECT_EQ(child->get_BoundingRectangle(&area), UIA_E_ELEMENTNOTAVAILABLE);
    EXPECT_EQ(child->SetFocus(), UIA_E_ELEMENTNOTAVAILABLE);
}

/// @brief Verify fragment navigation uses semantic parents.
TEST_F(native_accessibility_test, FragmentNavigationUsesSemanticParents)
{
    Microsoft::WRL::ComPtr<detail::provider> root;
    root.Attach(new detail::provider{endpoint});
    Microsoft::WRL::ComPtr<IRawElementProviderFragment> child;
    ASSERT_EQ(root->Navigate(NavigateDirection_FirstChild, &child), S_OK);
    ASSERT_NE(child.Get(), nullptr);
    EXPECT_EQ(child->SetFocus(), S_OK);
    ASSERT_EQ(channel->take_requests().size(), 1);
    Microsoft::WRL::ComPtr<IRawElementProviderFragment> parent;
    EXPECT_EQ(child->Navigate(NavigateDirection_Parent, &parent), S_OK);
    EXPECT_NE(parent.Get(), nullptr);
}

/// @brief Verify adapter attaches publishes and restores native dispatch.
TEST_F(native_accessibility_test, AdapterAttachesPublishesAndRestoresNativeDispatch)
{
    const auto original = target.state();
    adapter host{target, channel};
    const auto coordinates = host.coordinates();
    EXPECT_GT(coordinates.scale, 0);
    host.notify();
    button.focused = true;
    channel->publish({button});
    host.notify();
    EXPECT_EQ(target.state().width, original.width);
    channel->close();
    host.notify();
}

/// @brief A real UIA client discovers the custom HWND fragment and queues its Invoke pattern.
TEST_F(native_accessibility_test, UiAutomationClientFindsAndInvokesThePublishedButton)
{
    adapter host{target, channel};
    std::atomic<HRESULT> result{E_PENDING};
    std::thread client(
        [&]
        {
            const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            if (FAILED(initialized))
            {
                result.store(initialized);
                return;
            }

            HRESULT status = E_FAIL;
            {
                Microsoft::WRL::ComPtr<IUIAutomation2> automation;
                status = CoCreateInstance(CLSID_CUIAutomation8, nullptr, CLSCTX_INPROC_SERVER,
                                          IID_PPV_ARGS(automation.GetAddressOf()));
                if (SUCCEEDED(status))
                {
                    automation->put_ConnectionTimeout(1000);
                    automation->put_TransactionTimeout(1000);
                    Microsoft::WRL::ComPtr<IUIAutomationElement> root;
                    status = automation->ElementFromHandle(endpoint->window, &root);
                    if (SUCCEEDED(status))
                    {
                        VARIANT name{};
                        name.vt = VT_BSTR;
                        name.bstrVal = SysAllocString(L"Resume");
                        Microsoft::WRL::ComPtr<IUIAutomationCondition> condition;
                        status = automation->CreatePropertyCondition(UIA_NamePropertyId, name,
                                                                     &condition);
                        VariantClear(&name);
                        Microsoft::WRL::ComPtr<IUIAutomationElement> element;
                        if (SUCCEEDED(status))
                        {
                            status =
                                root->FindFirst(TreeScope_Descendants, condition.Get(), &element);
                        }

                        if (SUCCEEDED(status) && !element)
                        {
                            status = E_FAIL;
                        }

                        if (SUCCEEDED(status))
                        {
                            Microsoft::WRL::ComPtr<IUIAutomationInvokePattern> invoke;
                            status = element->GetCurrentPatternAs(
                                UIA_InvokePatternId, IID_PPV_ARGS(invoke.GetAddressOf()));
                            if (SUCCEEDED(status))
                            {
                                status = invoke->Invoke();
                            }
                        }
                    }
                }
            }
            CoUninitialize();
            result.store(status);
        });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (result.load() == E_PENDING && std::chrono::steady_clock::now() < deadline)
    {
        target.poll_events();
        target.wait_for_events(1);
    }

    EXPECT_NE(result.load(), E_PENDING) << "UIA client exceeded the diagnostic deadline";
    client.join();
    EXPECT_EQ(result.load(), S_OK);
    const auto pending = channel->take_requests();
    EXPECT_EQ(std::ranges::count(pending, accessibility::action::invoke,
                                 &accessibility::request::operation),
              1);
    for (const auto& request : pending)
    {
        EXPECT_EQ(request.target, button.id);
        EXPECT_TRUE(request.operation == accessibility::action::invoke ||
                    request.operation == accessibility::action::focus);
    }
}
} // namespace atlas::accessibility_host::tests
