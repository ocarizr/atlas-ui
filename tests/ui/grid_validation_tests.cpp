/** @file @brief Parameterized invalid track policies never become immutable layout definitions. */
#include <atlas/ui/grid_layout.hpp>
#include <gtest/gtest.h>
#include <limits>

namespace atlas::ui::tests
{
/// @brief Readable cases share one isolated definition builder per fact.
class invalid_grid_test : public ::testing::TestWithParam<unsigned>
{
};

/// @brief Reject malformed numeric, enum and bounded-topology inputs before retaining a policy.
TEST_P(invalid_grid_test, rejects_invalid_definition)
{
    grid_definition definition;
    switch (GetParam())
    {
        case 0:
            definition.columns.clear();
            break;
        case 1:
            definition.column_gap = -1;
            break;
        case 2:
            definition.row_gap = std::numeric_limits<float>::infinity();
            break;
        case 3:
            definition.columns[0].value = 0;
            break;
        case 4:
            definition.columns[0] = {grid_unit::automatic, 1};
            break;
        case 5:
            // Deliberately exercise rejection of an unknown, representable wire value.
            // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
            definition.columns[0].unit = static_cast<grid_unit>(255);
            break;
        case 6:
            definition.columns[0].value = std::numeric_limits<float>::quiet_NaN();
            break;
        case 7:
            definition.rows.resize(257);
            definition.columns.resize(256);
            break;
        default:
            definition.columns.resize(65537);
            break;
    }

    EXPECT_THROW((void)grid_layout{std::move(definition)}, std::invalid_argument);
}

/// @brief Stable indexes label independent policy failures without shared mutable state.
INSTANTIATE_TEST_SUITE_P(policies, invalid_grid_test, ::testing::Range(0U, 9U));
} // namespace atlas::ui::tests
