/** @file @brief Atomic admission preserves earlier demands when a scripted batch is rejected. */
#include <atlas/mvvm/view_model.hpp>
#include <gtest/gtest.h>

namespace atlas::mvvm::tests
{
/// @brief Mixed field deltas and independent signal namespaces share one atomic admission API.
TEST(mvvm_batch, AdmitsAllSupportedCommandKinds)
{
    view_model model{compile(
        {"state", {{"count", std::int64_t{0}}, {"ratio", 0.0}}, {{"go", {}}}, {{"done", {}}}})};
    const auto count = model.description().field("count");
    const auto ratio = model.description().field("ratio");
    model.begin_frame(1);
    const std::vector<command> batch{{count, update_kind::assign, {std::int64_t{2}}},
                                     {count, update_kind::add_integer, {std::int64_t{3}}},
                                     {ratio, update_kind::add_number, {1.5}},
                                     {model.description().action("go"), update_kind::assign, {}},
                                     {model.description().event("done"), update_kind::assign, {}}};
    model.submit(batch);
    model.flush();
    EXPECT_EQ(model.integer(count), 5);
    EXPECT_EQ(model.number(ratio), 1.5);
}

/// @brief Exact numeric protocols reject bool deltas without surfacing variant internals.
TEST(mvvm_batch, RejectsWrongRelativeArgumentType)
{
    view_model model{compile({"state", {{"count", std::int64_t{0}}}})};
    model.begin_frame(1);
    const std::vector<command> batch{
        {model.description().field("count"), update_kind::add_integer, {true}}};
    EXPECT_THROW(model.submit(batch), std::invalid_argument);
    EXPECT_EQ(model.inspect().commands, 0);
}

/// @brief A late invalid write cannot leak an earlier write or discard preexisting demands.
TEST(mvvm_batch, RejectsWholeBatchWithoutChangingPriorQueue)
{
    view_model model{compile({"state", {{"count", std::int64_t{0}}}})};
    const auto count = model.description().field("count");
    model.begin_frame(1);
    model.set(count, std::int64_t{2});
    const std::vector<command> batch{{count, update_kind::assign, {std::int64_t{3}}},
                                     {count, update_kind::assign, {true}}};
    EXPECT_THROW(model.submit(batch), std::invalid_argument);
    model.flush();
    EXPECT_EQ(model.integer(count), 2);
    EXPECT_EQ(model.inspect().commands, 1);
}
} // namespace atlas::mvvm::tests
