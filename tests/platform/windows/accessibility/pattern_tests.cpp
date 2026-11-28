/** @file @brief Selection, toggle and read-only range capabilities match semantic menu roles. */
#include "provider_fixture.hpp"
#include <memory>

namespace atlas::accessibility_host::tests
{
/// @brief A tab group may temporarily have no selection while its owner rebuilds content.
TEST_F(native_accessibility_test, TabsPermitTransientEmptySelection)
{
    accessibility::node group{{0, 1, 1}, {}, accessibility::role::tab_group, "Settings"};
    button.parent = group.id;
    button.kind = accessibility::role::tab;
    button.invoke = false;
    button.select = true;
    channel->publish({group, button});
    Microsoft::WRL::ComPtr<detail::provider> tabs;
    tabs.Attach(new detail::provider{endpoint, group.id});
    BOOL required = TRUE;
    ASSERT_EQ(tabs->get_IsSelectionRequired(&required), S_OK);
    EXPECT_FALSE(required);
    SAFEARRAY* selection = nullptr;
    ASSERT_EQ(tabs->GetSelection(&selection), S_OK);
    if (selection == nullptr)
    {
        ADD_FAILURE() << "selection must return an array, even when empty";
        return;
    }

    const std::unique_ptr<SAFEARRAY, decltype(&SafeArrayDestroy)> selection_owner{selection,
                                                                                  SafeArrayDestroy};
    LONG upper = 0;
    EXPECT_EQ(SafeArrayGetUBound(selection, 1, &upper), S_OK);
    EXPECT_EQ(upper, -1);
    button.selected = true;
    channel->publish({group, button});
    required = TRUE;
    ASSERT_EQ(tabs->get_IsSelectionRequired(&required), S_OK);
    EXPECT_FALSE(required);
    EXPECT_TRUE(channel->take_requests().empty());
}

/// @brief Tabs expose selection instead of Invoke and return retained selected-child interfaces.
TEST_F(native_accessibility_test, TabsExposeSingleSelectionWithoutInvoke)
{
    accessibility::node group{{0, 1, 1}, {}, accessibility::role::tab_group, "Settings"};
    button.parent = group.id;
    button.kind = accessibility::role::tab;
    button.invoke = false;
    button.select = true;
    button.selected = true;
    channel->publish({group, button});
    Microsoft::WRL::ComPtr<detail::provider> root;
    root.Attach(new detail::provider{endpoint, group.id});
    SAFEARRAY* selected = nullptr;
    ASSERT_EQ(root->GetSelection(&selected), S_OK);
    if (selected == nullptr)
    {
        ADD_FAILURE() << "selection must return an array";
        return;
    }

    LONG upper = -1;
    EXPECT_EQ(SafeArrayGetUBound(selected, 1, &upper), S_OK);
    EXPECT_EQ(upper, 0);
    SafeArrayDestroy(selected);
    Microsoft::WRL::ComPtr<detail::provider> tab;
    tab.Attach(new detail::provider{endpoint, button.id});
    Microsoft::WRL::ComPtr<IUnknown> pattern;
    EXPECT_EQ(tab->GetPatternProvider(UIA_InvokePatternId, &pattern), S_OK);
    EXPECT_EQ(pattern.Get(), nullptr);
    EXPECT_EQ(tab->Select(), S_OK);
    const auto requests = channel->take_requests();
    ASSERT_EQ(requests.size(), 1);
    EXPECT_EQ(requests.front().operation, accessibility::action::select);
}

/// @brief Checking changes stay on the owner; the reader receives the published checked state.
TEST_F(native_accessibility_test, CheckboxToggleQueuesItsAuthoredAction)
{
    button.kind = accessibility::role::checkbox;
    button.checked = true;
    channel->publish({button});
    Microsoft::WRL::ComPtr<detail::provider> control;
    control.Attach(new detail::provider{endpoint, button.id});
    ToggleState state = ToggleState_Off;
    EXPECT_EQ(control->get_ToggleState(&state), S_OK);
    EXPECT_EQ(state, ToggleState_On);
    EXPECT_EQ(control->Toggle(), S_OK);
    EXPECT_EQ(channel->take_requests().size(), 1);
}

/// @brief Read-only numeric values report bounds but reject mutation without enqueuing work.
TEST_F(native_accessibility_test, NumericRangeReportsReadOnlyValueAndRejectsWrites)
{
    button.kind = accessibility::role::slider;
    button.invoke = false;
    button.number = 5;
    button.maximum = 10;
    channel->publish({button});
    Microsoft::WRL::ComPtr<detail::provider> control;
    control.Attach(new detail::provider{endpoint, button.id});
    double value = 0;
    EXPECT_EQ(control->get_Value(&value), S_OK);
    EXPECT_EQ(value, 5);
    EXPECT_EQ(control->get_Maximum(&value), S_OK);
    EXPECT_EQ(value, 10);
    BOOL read_only = FALSE;
    EXPECT_EQ(control->get_IsReadOnly(&read_only), S_OK);
    EXPECT_TRUE(read_only);
    EXPECT_EQ(control->SetValue(6.0), UIA_E_INVALIDOPERATION);
    EXPECT_TRUE(channel->take_requests().empty());
}
} // namespace atlas::accessibility_host::tests
