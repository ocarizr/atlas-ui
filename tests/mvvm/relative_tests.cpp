/** @file @brief Accumulative demands preserve independent actions without stale read/modify/write.
 */
#include <atlas/mvvm/view_model.hpp>
#include <gtest/gtest.h>
#include <limits>

namespace atlas::mvvm::tests
{
/// @brief Repeated actions can add independent deltas before the next mutation boundary.
TEST(mvvm_relative, PreservesEveryIntegerDeltaAndOrderedAbsoluteWrites)
{
    const auto schema = compile({"model", {{"xp", std::int64_t{0}}}});
    view_model model{schema};
    const auto xp = schema.field("xp");
    model.begin_frame(1);
    model.add_integer(xp, 5);
    model.add_integer(xp, 5);
    model.flush();
    EXPECT_EQ(model.integer(xp), 10);
    model.set(xp, std::int64_t{20});
    model.add_integer(xp, -2);
    model.set(xp, std::int64_t{3});
    model.add_integer(xp, 4);
    model.flush();
    EXPECT_EQ(model.integer(xp), 7);
}

/// @brief Both signed boundaries are checked before adding the queued delta.
class integer_overflow : public ::testing::TestWithParam<std::pair<std::int64_t, std::int64_t>>
{
};

/// @brief Overflow fails without wrapping or changing the committed field.
TEST_P(integer_overflow, RejectsAtCommitBoundary)
{
    const auto [initial, delta] = GetParam();
    const auto schema = compile({"model", {{"xp", initial}}});
    view_model model{schema};
    const auto xp = schema.field("xp");
    model.begin_frame(1);
    model.add_integer(xp, delta);
    EXPECT_THROW(model.flush(), std::overflow_error);
    EXPECT_EQ(model.integer(xp), initial);
    EXPECT_TRUE(model.inspect().failed);
}

/// @brief Keep boundary identities readable in CTest reports.
std::string
overflow_name(const ::testing::TestParamInfo<std::pair<std::int64_t, std::int64_t>>& info)
{
    return info.index == 0 ? "PositiveOverflow" : "NegativeOverflow";
}

INSTANTIATE_TEST_SUITE_P(
    boundaries, integer_overflow,
    ::testing::Values(std::pair{std::numeric_limits<std::int64_t>::max(), std::int64_t{1}},
                      std::pair{std::numeric_limits<std::int64_t>::min(), std::int64_t{-1}}),
    overflow_name);

/// @brief Number deltas retain finite type/domain checks independently of UI conversion.
TEST(mvvm_relative, PreservesNumberDeltasAndRejectsNonfiniteResults)
{
    const auto schema = compile({"model", {{"number", 0.5}}});
    view_model model{schema};
    const auto number = schema.field("number");
    model.begin_frame(1);
    model.add_number(number, 0.25);
    model.add_number(number, 0.25);
    model.flush();
    EXPECT_DOUBLE_EQ(model.number(number), 1);
    EXPECT_THROW(model.add_integer(number, 1), std::invalid_argument);
    model.set(number, std::numeric_limits<double>::max());
    model.flush();
    model.add_number(number, std::numeric_limits<double>::max());
    EXPECT_THROW(model.flush(), std::overflow_error);
    EXPECT_DOUBLE_EQ(model.number(number), std::numeric_limits<double>::max());
}
} // namespace atlas::mvvm::tests
