/** @file @brief Isolated native window and semantic endpoint fixture. */
#pragma once
#include "provider.hpp"
#include <atlas/accessibility_host/adapter.hpp>
#include <gtest/gtest.h>
#include <wrl/client.h>

namespace atlas::accessibility_host::tests
{
/// @brief Hidden native host and independent channel, disconnected before releasing its window.
class native_accessibility_test : public ::testing::Test
{
  protected:
    window::window target{{"Accessible native fixture", 80, 60, false}}; ///< Owner window.
    std::shared_ptr<accessibility::channel> channel =
        std::make_shared<accessibility::channel>(); ///< Endpoint.
    std::shared_ptr<detail::endpoint> endpoint =
        std::make_shared<detail::endpoint>(); ///< Client lifetime.
    accessibility::node button{
        {1, 1, 1}, {}, accessibility::role::button, "Resume"}; ///< Semantic control.

    void SetUp() override
    {
        endpoint->window = FindWindowW(L"atlas_presentation_window", L"Accessible native fixture");
        endpoint->source = channel;
        button.focusable = true;
        button.invoke = true;
        channel->publish({button});
    } ///< Publish independently of any GPU or active user session.
};

} // namespace atlas::accessibility_host::tests
