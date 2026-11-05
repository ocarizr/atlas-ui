/** @file @brief Owner ingress, admission rollback and observer failure boundaries. */
#include "input_test_support.hpp"
#include <future>

namespace atlas::input::tests
{
/// @brief Overflow is explicit and publication never waits for a consumer.
TEST(input_ingress, OverflowResetsTheSourceAndForeignPublicationDoesNotMutateIt)
{
    auto queue = std::make_unique<event_queue>();
    EXPECT_FALSE(std::async(std::launch::async,
                            [&]
                            {
                                return queue->try_push(key_sample(key::a, true));
                            })
                     .get());
    EXPECT_TRUE(queue->drain().values().empty());
    for (unsigned i = 0; i < 256; ++i)
    {
        EXPECT_TRUE(queue->try_push(key_sample(key::a, true)));
    }

    EXPECT_FALSE(queue->try_push(key_sample(key::a, false)));
    const auto batch = queue->drain();
    EXPECT_TRUE(batch.overflow);
    ASSERT_EQ(batch.values().size(), 1);
    EXPECT_EQ(batch.values()[0].kind, event_kind::source_reset);
    EXPECT_EQ(batch.values()[0].source, queue->source());
    EXPECT_TRUE(queue->try_push(key_sample(key::a, false)));
}

/// @brief Metadata overflow is rejected before raw callbacks or frame consumption.
TEST(input_failures, WholeBatchCapacityFailureCanRetryTheSameFrame)
{
    input_system system{{.max_controls = 1}};
    recorder listener;
    const std::array invalid{key_sample(key::a, true), key_sample(key::b, true)};
    EXPECT_THROW(system.dispatch(1, 0, invalid, listener), std::overflow_error);
    EXPECT_TRUE(listener.raw.empty());
    EXPECT_EQ(system.statistics().frame, 0);
    const std::array valid{key_sample(key::a, true)};
    EXPECT_NO_THROW(system.dispatch(1, 0, valid, listener));
}

/// @brief Client failure latches processing and a reset cannot retain an old press.
TEST(input_failures, HandlerFailureRequiresExplicitNeutralRecovery)
{
    input_system system;
    system.define_action({1, "confirm"});
    const std::array map{binding{1, {device_kind::keyboard, 13}}};
    system.define_context({1, "ui", 0, true, activation_policy::require_neutral, map});
    recorder listener;
    listener.action = [](const auto&)
    {
        throw std::runtime_error("listener failure");
    };
    const std::array down{key_sample(key::enter, true)};
    EXPECT_THROW(system.dispatch(1, 0, down, listener), std::runtime_error);
    EXPECT_TRUE(system.statistics().failed);
    EXPECT_THROW(system.dispatch(2, 0.1, {}, listener), std::logic_error);
    system.reset();
    listener.action = {};
    listener.actions.clear();
    EXPECT_NO_THROW(system.dispatch(2, 0.1, {}, listener));
    EXPECT_TRUE(listener.actions.empty());
}
} // namespace atlas::input::tests
