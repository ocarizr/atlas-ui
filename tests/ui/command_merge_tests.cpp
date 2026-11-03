/** @file @brief Absolute-write coalescing preserves provenance and frame/batch boundaries. */
#include <atlas/ui/commands.hpp>
#include <gtest/gtest.h>

namespace atlas::ui::tests
{
/// @brief Each case owns a producer and starts one explicit frame.
class command_merge_test : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        buffer.begin_frame(1);
    } ///< Isolated frame history for every test.

    command_buffer buffer;              ///< No shared mutable state across cases.
    const element_handle element{0, 1}; ///< A complete, non-null opaque identity.
};

/// @brief Independent keys survive, and duplicate writes select the last admitted assignment.
TEST_F(command_merge_test, merges_by_key_and_publishes_in_sequence_order)
{
    buffer.set(element, property::width, 10.f, 1);
    buffer.set(element, property::height, 20.f, 2);
    buffer.set(element, property::width, 30.f, 3);
    const auto batch = buffer.seal();
    ASSERT_EQ(batch.writes().size(), 2U);
    EXPECT_EQ(batch.writes()[0].field, property::height);
    EXPECT_EQ(batch.writes()[1].value, property_value{30.f});
    ASSERT_EQ(batch.conflicts().size(), 1U);
    const auto& conflict = batch.conflicts()[0];
    EXPECT_EQ(conflict.previous.producer, 1U);
    EXPECT_EQ(conflict.replacement.producer, 3U);
    EXPECT_EQ(conflict.replacement.sequence, 3U);
    EXPECT_EQ(conflict.writes_this_frame, 2U);
    EXPECT_EQ(batch.frame(), 1U);
    EXPECT_EQ(batch.ordinal(), 1U);
    EXPECT_EQ(buffer.pending(), 0U);
}

/// @brief Equal values and a single producer still represent multiple writes requiring review.
TEST_F(command_merge_test, identical_assignments_are_reported_as_conflicts)
{
    buffer.set(element, property::visible, true, 5);
    buffer.set(element, property::visible, true, 5);
    const auto batch = buffer.seal();
    EXPECT_EQ(batch.writes().size(), 1U);
    ASSERT_EQ(batch.conflicts().size(), 1U);
    EXPECT_EQ(batch.conflicts()[0].writes_this_frame, 2U);
}

/// @brief Sync boundaries preserve earlier packets but retain conflict history for the frame.
TEST_F(command_merge_test, detects_cross_batch_writes_without_rewriting_published_data)
{
    buffer.set(element, property::width, 10.f, 1);
    buffer.set(element, property::height, 40.f, 1);
    const auto first = buffer.seal();
    buffer.set(element, property::width, 20.f, 2);
    const auto second = buffer.seal();
    EXPECT_EQ(second.writes().size(), 1U);
    ASSERT_EQ(second.conflicts().size(), 1U);
    EXPECT_EQ(second.ordinal(), 2U);
    EXPECT_EQ(second.conflicts()[0].previous.sequence, 1U);
    EXPECT_EQ(first.writes()[0].value, property_value{10.f});
    buffer.begin_frame(2);
    buffer.set(element, property::width, 30.f, 3);
    EXPECT_TRUE(buffer.seal().conflicts().empty());
    EXPECT_EQ(first.writes()[0].value, property_value{10.f});
}

/// @brief Slot generations and properties are separate keys, even when slot indices match.
TEST_F(command_merge_test, does_not_merge_distinct_properties_or_generations)
{
    buffer.set(element, property::width, 10.f, 1);
    buffer.set({0, 2}, property::width, 20.f, 1);
    buffer.set(element, property::height, 30.f, 1);
    const auto batch = buffer.seal();
    EXPECT_EQ(batch.writes().size(), 3U);
    EXPECT_TRUE(batch.conflicts().empty());
}

/// @brief Equal slot/generation/property values in different sessions never coalesce.
TEST(command_merge, standalone_keys_include_the_session_identity)
{
    command_buffer commands;
    commands.begin_frame(1);
    commands.set({0, 1, 1}, property::width, 10.f, 1);
    commands.set({0, 1, 2}, property::width, 20.f, 1);
    commands.set({0, 1, 1}, property::width, 30.f, 1);
    const auto batch = commands.seal();
    ASSERT_EQ(batch.writes().size(), 2U);
    EXPECT_EQ(batch.writes()[0].target.session, 2U);
    EXPECT_EQ(batch.writes()[1].target.session, 1U);
    EXPECT_EQ(batch.writes()[1].value, property_value{30.f});
    ASSERT_EQ(batch.conflicts().size(), 1U);
    EXPECT_EQ(batch.conflicts()[0].replacement.target.session, 1U);
}
} // namespace atlas::ui::tests
