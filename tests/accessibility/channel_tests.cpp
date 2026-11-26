/** @file @brief Portable semantic publication and request lifetime facts. */
#include <atlas/accessibility/channel.hpp>
#include <gtest/gtest.h>
#include <thread>

namespace atlas::accessibility::tests
{
/// @brief One independently owned semantic root and button per fact.
class channel_test : public ::testing::Test
{
  protected:
    node root{{0, 1, 1}, {}, role::group, "Menu"};           ///< Semantic parent.
    node button{{1, 1, 1}, root.id, role::button, "Resume"}; ///< Actionable child.
    channel output;                                          ///< Independent bounded endpoint.

    void publish()
    {
        button.focusable = true;
        button.invoke = true;
        output.publish({root, button});
    } ///< Publish a valid initial view.
};

/// @brief Verify requests are queued without running application code.
TEST_F(channel_test, RequestsAreQueuedWithoutRunningApplicationCode)
{
    publish();
    std::thread client(
        [&]
        {
            EXPECT_TRUE(output.request({button.id, action::invoke}));
        });
    client.join();
    const auto pending = output.take_requests();
    ASSERT_EQ(pending.size(), 1);
    EXPECT_EQ(pending.front().target, button.id);
}

/// @brief Verify invalid publication preserves the previous snapshot.
TEST_F(channel_test, InvalidPublicationPreservesThePreviousSnapshot)
{
    publish();
    const auto before = output.read();
    button.parent = button.id;
    EXPECT_THROW(output.publish({root, button}), std::invalid_argument);
    EXPECT_EQ(output.read(), before);
}

/// @brief Verify recycled identities never receive old requests.
TEST_F(channel_test, RecycledIdentitiesNeverReceiveOldRequests)
{
    publish();
    EXPECT_TRUE(output.request({button.id, action::invoke}));
    const auto old = button.id;
    ++button.id.generation;
    publish();
    EXPECT_FALSE(output.request({old, action::invoke}));
    EXPECT_TRUE(output.take_requests().empty());
}

/// @brief Verify disabled controls remain readable without accepting actions.
TEST_F(channel_test, DisabledControlsRemainReadableWithoutAcceptingActions)
{
    button.enabled = false;
    publish();
    ASSERT_EQ(output.read()->nodes().size(), 2);
    EXPECT_FALSE(output.request({button.id, action::invoke}));
}

/// @brief Verify close disconnects retained clients and cancels requests.
TEST_F(channel_test, CloseDisconnectsRetainedClientsAndCancelsRequests)
{
    publish();
    auto retained = output.read();
    EXPECT_TRUE(output.request({button.id, action::focus}));
    output.close();
    EXPECT_EQ(retained->nodes().size(), 2);
    EXPECT_FALSE(output.read());
    EXPECT_FALSE(output.request({button.id, action::focus}));
    EXPECT_TRUE(output.take_requests().empty());
    EXPECT_THROW(output.publish({root}), std::logic_error);
}

/// @brief Verify queue overflow does not drop an admitted request.
TEST_F(channel_test, QueueOverflowDoesNotDropAnAdmittedRequest)
{
    channel limited{{.max_requests = 1}};
    button.focusable = true;
    limited.publish({root, button});
    EXPECT_TRUE(limited.request({button.id, action::focus}));
    EXPECT_FALSE(limited.request({button.id, action::focus}));
    EXPECT_EQ(limited.take_requests().size(), 1);
}

/// @brief Verify strings and hierarchy are bounded before publication.
TEST_F(channel_test, StringsAndHierarchyAreBoundedBeforePublication)
{
    channel limited{{.max_nodes = 1, .max_text_bytes = 3}};
    EXPECT_THROW(limited.publish({root, button}), std::length_error);
    EXPECT_THROW(limited.publish({root}), std::length_error);
    root.name = std::string(1, '\0');
    EXPECT_THROW(output.publish({root}), std::invalid_argument);
}

/// @brief Verify mutation requires the owner thread.
TEST_F(channel_test, MutationRequiresTheOwnerThread)
{
    std::thread client(
        [&]
        {
            EXPECT_THROW(output.publish({root}), std::logic_error);
        });
    client.join();
}

/// @brief Invalid physical clipping cannot enter a published native hit-test snapshot.
TEST_F(channel_test, RejectsInvalidClipGeometry)
{
    publish();
    const auto before = output.read();
    button.clipped = true;
    button.clip.width = -1;
    EXPECT_THROW(output.publish({root, button}), std::invalid_argument);
    EXPECT_EQ(output.read(), before);
}

/// @brief Sorted validation keeps semantic parent order independent of numeric slot order.
TEST_F(channel_test, ValidatesParentOrderRatherThanNumericIdentityOrder)
{
    root.id.index = 9;
    button.parent = root.id;
    publish();
    EXPECT_EQ(output.read()->nodes().size(), 2);
    EXPECT_THROW(output.publish({button, root}), std::invalid_argument);
    EXPECT_THROW(output.publish({root, root}), std::invalid_argument);
}
} // namespace atlas::accessibility::tests
