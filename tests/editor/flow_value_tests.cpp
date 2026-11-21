/** @file @brief Exact typed scalar edits preserve precision and reject invalid numeric boundaries.
 */
#include "flow_values.hpp"
#include <gtest/gtest.h>

namespace atlas::editor::tests
{
/// @brief A readable label accompanies each exact double authoring scenario.
struct scalar_case
{
    const char* name; ///< Stable test case identity.
    double value;     ///< Authored scalar that must survive a no-op text edit.
};

/// @brief No shared mutation or filesystem state is needed for exact scalar round trips.
class flow_value_test : public testing::TestWithParam<scalar_case>
{
};

/// @brief Merely focusing and confirming a numeric field must never round away authored precision.
TEST_P(flow_value_test, RetainsExactValueThroughInspectorText)
{
    const mvvm::value value = GetParam().value;
    EXPECT_EQ(parse_flow_value(value, flow_value(value)), value);
}

INSTANTIATE_TEST_SUITE_P(numbers, flow_value_test,
                         testing::Values(scalar_case{"zero", 0.0}, scalar_case{"fraction", -2.5},
                                         scalar_case{"precision", 1.0000000000000002},
                                         scalar_case{"tiny", 1e-20}),
                         [](const auto& value)
                         {
                             return value.param.name;
                         });

/// @brief Stepping never wraps integral boundaries or coerces unrelated text and booleans.
TEST(flow_value, RejectsOverflowAndInvalidScalarText)
{
    EXPECT_THROW((void)step_flow_number("9223372036854775807", 1), std::invalid_argument);
    EXPECT_THROW((void)step_flow_number("-9223372036854775808", -1), std::invalid_argument);
    EXPECT_THROW((void)parse_flow_value(mvvm::value{false}, "yes"), std::invalid_argument);
    EXPECT_THROW((void)parse_flow_value(mvvm::value{0.0}, "nan"), std::invalid_argument);
    EXPECT_THROW((void)parse_flow_value(mvvm::value{std::int64_t{}}, "1.5"), std::invalid_argument);
    EXPECT_EQ(step_flow_number("0", -1), "-1");
}
} // namespace atlas::editor::tests
