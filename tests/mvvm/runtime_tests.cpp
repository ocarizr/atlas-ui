/** @file @brief Observable state, action dispatch and bounded reentrant demands. */
#include <array>
#include <atlas/mvvm/view_model.hpp>
#include <gtest/gtest.h>

namespace atlas::mvvm::tests
{
/// @brief Keep notification values rather than borrowed callback spans.
class recorder final : public observer
{
  public:
    std::vector<value> values; ///< Copies for assertion after dispatch.

    void on_notify(const notification& event, view_model&) override
    {
        if (!event.arguments.empty())
        {
            values.push_back(event.arguments.front());
        }
    } ///< Record each delivered value independently of its lifetime.
};

/// @brief XML/codegen schemas share typed handles and instance-local state.
TEST(mvvm_state, CoalescesWritesAndPublishesOnlyChangedValues)
{
    definition source{
        "hud", {{"xp", std::int64_t{0}}}, {{"gain", {{"amount", value_type::integer}}}}, {}};
    const auto schema = compile(source);
    view_model model{schema};
    const auto xp = schema.field("xp");
    auto listener = std::make_shared<recorder>();
    model.subscribe(xp, listener);
    model.begin_frame(1);
    model.set(xp, std::int64_t{4});
    model.set(xp, std::int64_t{9});
    EXPECT_EQ(model.integer(xp), 0);
    model.flush();
    EXPECT_EQ(model.integer(xp), 9);
    ASSERT_EQ(listener->values.size(), 1);
    model.set(xp, std::int64_t{9});
    model.flush();
    EXPECT_EQ(listener->values.size(), 1);
}

/// @brief Named actions are reusable protocol messages, not calls into UI widgets.
TEST(mvvm_state, ActionsProduceDeferredFieldDemands)
{
    /// @brief Generic native behavior for the declared action protocol.
    class gain_handler final : public observer
    {
      public:
        field_handle xp; ///< Cached slot resolved before dispatch.

        void on_notify(const notification& event, view_model& model) override
        {
            model.add_integer(xp, std::get<std::int64_t>(event.arguments.front()));
        } ///< Produce a later write, never recursively flush the model.
    };

    const auto schema = compile(
        {"hud", {{"xp", std::int64_t{0}}}, {{"gain", {{"amount", value_type::integer}}}}, {}});
    view_model model{schema};
    auto handler = std::make_shared<gain_handler>();
    handler->xp = schema.field("xp");
    model.subscribe(schema.action("gain"), handler);
    model.begin_frame(1);
    const std::array args{value{std::int64_t{5}}};
    model.invoke(schema.action("gain"), args);
    model.flush();
    EXPECT_EQ(model.integer(handler->xp), 5);
}

/// @brief Text payloads retain independent notification/read ownership after queue storage retires.
TEST(mvvm_state, MovesTextDemandsAndKeepsOwnedNotificationsAfterFlush)
{
    const auto schema = compile({"model", {{"title", std::string{"old"}}}});
    view_model model{schema};
    const auto title = schema.field("title");
    auto listener = std::make_shared<recorder>();
    model.subscribe(title, listener);
    std::string payload(256, 'x');
    const auto expected = payload;
    model.begin_frame(1);
    model.set(title, std::move(payload));
    model.flush();
    EXPECT_EQ(model.text(title), expected);
    ASSERT_EQ(listener->values.size(), 1);
    EXPECT_EQ(std::get<std::string>(listener->values.front()), expected);
    const auto copy = model.read(title);
    model.set(title, std::string{"new"});
    model.flush();
    EXPECT_EQ(std::get<std::string>(copy), expected);
    EXPECT_EQ(model.text(title), "new");
}
} // namespace atlas::mvvm::tests
