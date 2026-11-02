/** @file @brief Invalid and saturated admission cannot partially alter a command batch. */
#include <atlas/ui/commands.hpp>
#include <future>
#include <gtest/gtest.h>
#include <limits>

namespace atlas::ui::tests
{
/// @brief One property/type/range failure with a readable parameter identity.
struct invalid_write_case
{
    property field;       ///< Selected validation rule.
    property_value value; ///< Rejected input.
    const char* name;     ///< Stable GoogleTest suffix.
};

/// @brief Input variations share only immutable test parameters.
class invalid_value_test : public ::testing::TestWithParam<invalid_write_case>
{
};

/// @brief Properties reject mismatched types and invalid scalars before admission.
TEST_P(invalid_value_test, rejects_invalid_values_without_consuming_capacity)
{
    command_buffer buffer{{1, 2, 0}};
    buffer.begin_frame(1);
    EXPECT_THROW(buffer.set({0, 1}, GetParam().field, GetParam().value, 1), std::invalid_argument);
    EXPECT_EQ(buffer.pending(), 0U);
    buffer.set({0, 1}, property::width, 0.f, 1);
    EXPECT_EQ(buffer.seal().writes().size(), 1U);
}

INSTANTIATE_TEST_SUITE_P(
    values, invalid_value_test,
    ::testing::Values(invalid_write_case{property::width, true, "wrong_type"},
                      invalid_write_case{property::height, -1.f, "negative_extent"},
                      invalid_write_case{property::width, std::numeric_limits<float>::infinity(),
                                         "infinite"},
                      invalid_write_case{property::width, std::numeric_limits<float>::quiet_NaN(),
                                         "not_a_number"},
                      invalid_write_case{property::visible, 1.f, "non_boolean_visibility"},
                      invalid_write_case{property::opacity, 1.01f, "opacity_out_of_range"}),
    [](const ::testing::TestParamInfo<invalid_write_case>& info)
    {
        return info.param.name;
    });

/// @brief Previously sealed commands still count against the total per-frame admission limit.
TEST(command_validation, enforces_frame_and_batch_limits_without_silent_drops)
{
    command_buffer buffer{{2, 2, 0}};
    buffer.begin_frame(1);
    buffer.set({0, 1}, property::width, 0.f, 1);
    const auto first = buffer.seal();
    buffer.set({0, 1}, property::height, 0.f, 1);
    EXPECT_THROW(buffer.set({0, 1}, property::width, 1.f, 1), std::overflow_error);
    EXPECT_EQ(buffer.pending(), 1U);
    EXPECT_EQ(buffer.seal().writes().size(), 1U);
    EXPECT_THROW(buffer.seal(), std::overflow_error);
    buffer.begin_frame(2);
    EXPECT_TRUE(buffer.seal().writes().empty());
    EXPECT_EQ(first.writes().size(), 1U);
}

/// @brief Explicit rollback does not erase already sealed history or leave duplicate ordinals.
TEST(command_validation, rejects_bad_lifecycle_and_allows_pending_rollback)
{
    command_buffer buffer;
    EXPECT_THROW(buffer.set({0, 1}, property::width, 1.f, 1), std::logic_error);
    EXPECT_THROW(buffer.seal(), std::logic_error);
    EXPECT_THROW(buffer.begin_frame(0), std::invalid_argument);
    buffer.begin_frame(1);
    buffer.set({0, 1}, property::width, 1.f, 1);
    EXPECT_THROW(buffer.begin_frame(2), std::logic_error);
    buffer.discard_pending();
    EXPECT_EQ(buffer.pending(), 0U);
    EXPECT_THROW(buffer.begin_frame(1), std::invalid_argument);
    buffer.begin_frame(2);
    EXPECT_EQ(buffer.seal().ordinal(), 1U);
}

/// @brief Foreign calls fail before touching producer storage; published packets are readable.
TEST(command_validation, rejects_foreign_producers_and_retains_immutable_packets)
{
    command_buffer buffer;
    buffer.begin_frame(1);
    buffer.set({0, 1}, property::visible, true, 1);
    const auto batch = buffer.seal();
    auto reader = std::async(std::launch::async,
                             [&]
                             {
                                 EXPECT_THROW(buffer.set({0, 1}, property::visible, false, 2),
                                              std::logic_error);
                                 EXPECT_THROW(buffer.begin_frame(2), std::logic_error);
                                 EXPECT_THROW(buffer.seal(), std::logic_error);
                                 EXPECT_THROW(buffer.discard_pending(), std::logic_error);
                                 EXPECT_THROW(buffer.pending(), std::logic_error);
                                 EXPECT_EQ(batch.writes()[0].value, property_value{true});
                             });
    reader.get();
    EXPECT_EQ(buffer.pending(), 0U);
}

/// @brief Exhausted batch admission must reject writes that could no longer be published.
TEST(command_validation, rejects_writes_after_last_batch_without_changing_pending)
{
    command_buffer buffer{{4, 1, 0}};
    buffer.begin_frame(1);
    const auto empty = buffer.seal();
    EXPECT_THROW(buffer.set({0, 1}, property::width, 1.f, 1), std::overflow_error);
    EXPECT_EQ(buffer.pending(), 0U);
    EXPECT_EQ(empty.frame(), 1U);
}

/// @brief Nonzero identity fields are required even before a live tree is introduced.
TEST(command_validation, rejects_missing_target_generation_and_producer)
{
    command_buffer buffer;
    buffer.begin_frame(1);
    EXPECT_THROW(buffer.set({0, 0}, property::width, 1.f, 1), std::invalid_argument);
    EXPECT_THROW(buffer.set({0, 1}, property::width, 1.f, 0), std::invalid_argument);
    EXPECT_EQ(buffer.pending(), 0U);
}

/// @brief Exercise defensive enum validation with deliberately malformed caller input.
TEST(command_validation, rejects_unknown_property_without_admission)
{
    command_buffer buffer;
    buffer.begin_frame(1);
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    const auto unknown = static_cast<property>(255);
    EXPECT_THROW(buffer.set({0, 1}, unknown, 0.f, 1), std::invalid_argument);
    EXPECT_EQ(buffer.pending(), 0U);
}

/// @brief Unsupported budgets fail construction before starting a frame or accepting work.
class invalid_limits_test : public ::testing::TestWithParam<command_limits>
{
};

/// @brief Include empty and excessive work, phase and diagnostic budgets.
TEST_P(invalid_limits_test, rejects_unsupported_limits)
{
    EXPECT_THROW((command_buffer{GetParam()}), std::invalid_argument);
}

INSTANTIATE_TEST_SUITE_P(limits, invalid_limits_test,
                         ::testing::Values(command_limits{0, 2, 0}, command_limits{65537, 2, 0},
                                           command_limits{4, 0, 0}, command_limits{4, 1025, 0},
                                           command_limits{4, 2, 65}));
} // namespace atlas::ui::tests
