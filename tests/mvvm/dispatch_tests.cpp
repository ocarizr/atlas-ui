/** @file @brief Stable observer order, safe retirement, feedback budgets and partial-failure
 * recovery. */
#include <array>
#include <atlas/mvvm/view_model.hpp>
#include <functional>
#include <gtest/gtest.h>

namespace atlas::mvvm::tests
{
/// @brief One test owns its callback behavior; no mutable global dispatch state.
class callback_observer final : public observer
{
  public:
    std::function<void(const notification&, view_model&)> callback; ///< Per-test behavior.

    /// @brief Retain one isolated test behavior.
    explicit callback_observer(std::function<void(const notification&, view_model&)> action)
        : callback(std::move(action))
    {
    }

    ///< Retain isolated behavior for one subscription.
    /// @brief Invoke on the owner without worker scheduling.
    void on_notify(const notification& event, view_model& model) override
    {
        callback(event, model);
    }

    ///< Delegation remains synchronous on the model owner.
};

/// @brief Reusing vacant metadata cannot move a newly registered observer ahead of older ones.
TEST(mvvm_dispatch, ReusedSubscriptionsPreserveRegistrationOrder)
{
    const auto schema = compile({"model", {{"x", std::int64_t{0}}}});
    view_model model{schema};
    const auto field = schema.field("x");
    std::vector<int> order;
    const auto one = model.subscribe(field, std::make_shared<callback_observer>(
                                                [&](const auto&, auto&)
                                                {
                                                    order.push_back(1);
                                                }));
    model.subscribe(field, std::make_shared<callback_observer>(
                               [&](const auto&, auto&)
                               {
                                   order.push_back(2);
                               }));
    model.unsubscribe(one);
    model.subscribe(field, std::make_shared<callback_observer>(
                               [&](const auto&, auto&)
                               {
                                   order.push_back(3);
                               }));
    model.begin_frame(1);
    model.set(field, std::int64_t{1});
    model.flush();
    EXPECT_EQ(order, (std::vector{2, 3}));
}

/// @brief A faulty observer must not prevent healthy bindings observing an already committed field.
TEST(mvvm_dispatch, AttemptsRemainingObserversThenPreservesTheOriginalFailure)
{
    const auto schema = compile({"model", {{"x", std::int64_t{0}}}});
    view_model model{schema};
    const auto field = schema.field("x");
    bool notified = false;
    const auto faulty = model.subscribe(field, std::make_shared<callback_observer>(
                                                   [](const auto&, auto&)
                                                   {
                                                       throw std::runtime_error("observer failed");
                                                   }));
    model.subscribe(field, std::make_shared<callback_observer>(
                               [&](const auto&, auto&)
                               {
                                   notified = true;
                               }));
    model.begin_frame(1);
    model.set(field, std::int64_t{5});
    EXPECT_THROW(model.flush(), std::runtime_error);
    EXPECT_TRUE(notified);
    EXPECT_EQ(model.integer(field), 5);
    EXPECT_TRUE(model.inspect().failed);
    EXPECT_THROW(model.begin_frame(2), std::logic_error);
    model.unsubscribe(faulty);
    model.reset();
    model.begin_frame(2);
    EXPECT_NO_THROW(model.flush());
}

/// @brief Removing another listener inside a callback suppresses that listener immediately.
TEST(mvvm_dispatch, RetirementDuringNotificationSuppressesLaterDelivery)
{
    const auto schema = compile({"model", {{"x", false}}});
    view_model model{schema};
    const auto field = schema.field("x");
    subscription later;
    int calls = 0;
    model.subscribe(field, std::make_shared<callback_observer>(
                               [&](const auto&, auto& source)
                               {
                                   source.unsubscribe(later);
                               }));
    later = model.subscribe(field, std::make_shared<callback_observer>(
                                       [&](const auto&, auto&)
                                       {
                                           ++calls;
                                       }));
    model.begin_frame(1);
    model.set(field, true);
    model.flush();
    EXPECT_EQ(calls, 0);
}

/// @brief Alternating writes terminate at a deterministic budget rather than recursion/another
/// frame.
TEST(mvvm_dispatch, FeedbackCyclesExhaustTheFrameBudgetAndCanBeRecovered)
{
    const auto schema = compile({"model", {{"x", false}}});
    view_model model{schema, {.max_commands = 4}};
    const auto field = schema.field("x");
    const auto token = model.subscribe(field, std::make_shared<callback_observer>(
                                                  [&](const auto&, auto& source)
                                                  {
                                                      source.set(field, !source.boolean(field));
                                                  }));
    model.begin_frame(1);
    model.set(field, true);
    EXPECT_THROW(model.flush(), std::overflow_error);
    EXPECT_TRUE(model.inspect().failed);
    EXPECT_EQ(model.inspect().commands, 4);
    model.unsubscribe(token);
    model.reset();
    model.begin_frame(2);
    EXPECT_NO_THROW(model.flush());
}

/// @brief A callback may forward its borrowed arguments into another owned queued event.
TEST(mvvm_dispatch, RepeatedSignalsRemainOrderedAndBorrowedPayloadsCanBeForwarded)
{
    const auto schema = compile({"model",
                                 {},
                                 {{"send", {{"text", value_type::text}}}},
                                 {{"done", {{"text", value_type::text}}}}});
    view_model model{schema};
    int calls = 0;
    model.subscribe(schema.action("send"), std::make_shared<callback_observer>(
                                               [&](const auto& event, auto& source)
                                               {
                                                   source.emit(schema.event("done"),
                                                               event.arguments);
                                               }));
    model.subscribe(schema.event("done"),
                    std::make_shared<callback_observer>(
                        [&](const auto& event, auto&)
                        {
                            EXPECT_EQ(std::get<std::string>(event.arguments.front()), "payload");
                            ++calls;
                        }));
    model.begin_frame(1);
    const std::array args{value{std::string{"payload"}}};
    model.invoke(schema.action("send"), args);
    model.invoke(schema.action("send"), args);
    model.flush();
    EXPECT_EQ(calls, 2);
}

/// @brief Capacity rejection and middle-slot reuse preserve bucket ownership and stale tokens.
TEST(mvvm_dispatch, SubscriptionCapacityAndCrossFieldReuseRemainSafe)
{
    const auto schema = compile({"model", {{"a", false}, {"b", false}}});
    view_model model{schema, {.max_subscriptions = 3}};
    std::vector<int> calls;
    const auto first = model.subscribe(schema.field("a"), std::make_shared<callback_observer>(
                                                              [&](const auto&, auto&)
                                                              {
                                                                  calls.push_back(1);
                                                              }));
    const auto middle = model.subscribe(schema.field("a"), std::make_shared<callback_observer>(
                                                               [&](const auto&, auto&)
                                                               {
                                                                   calls.push_back(2);
                                                               }));
    auto listener = std::make_shared<callback_observer>(
        [&](const auto&, auto&)
        {
            calls.push_back(3);
        });
    model.subscribe(schema.field("a"), listener);
    EXPECT_THROW(model.subscribe(schema.field("b"), listener), std::overflow_error);
    model.unsubscribe(middle);
    model.subscribe(schema.field("b"), listener);
    model.unsubscribe(middle);
    model.begin_frame(1);
    model.set(schema.field("a"), true);
    model.set(schema.field("b"), true);
    model.flush();
    EXPECT_EQ(calls, (std::vector{1, 3, 3}));
    view_model other{schema};
    EXPECT_THROW(other.unsubscribe(first), std::invalid_argument);
}
} // namespace atlas::mvvm::tests
