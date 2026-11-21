/** @file @brief Incremental cross-view routing retains stage order and cumulative frame limits. */
#include "input_test_support.hpp"

namespace atlas::ui::tests
{
/// @brief Ordered cross-view packets do not duplicate system callbacks or bypass stage order.
TEST_F(input_fixture, RoutedChunksBroadcastProcessInputOnlyOnce)
{
    const auto node = tree.create();
    input.bind(node, handler);
    begin();
    const input_event event{input_kind::pointer_move, {2, 2}};
    EXPECT_THROW(input.process_more({&event, 1}), std::logic_error);
    input.process({});
    input.process_more({&event, 1});
    input.process_more({&event, 1});
    EXPECT_EQ(count(event_kind::process_input), 1U);
    EXPECT_EQ(input.statistics().events, 2U);
    input.tick();
    EXPECT_THROW(input.process_more({&event, 1}), std::logic_error);
    input.late_tick();
    input.end_frame();
    tree.apply(tree.commands().seal());
}

/// @brief Chunking cannot bypass frame limits or deliver a malformed replay prefix.
TEST_F(input_fixture, RoutedChunksShareEventBudgetAndRejectInvalidPacketsBeforeCallbacks)
{
    const auto node = tree.create();
    input.bind(node, handler);
    begin();
    std::vector<input_event> events(256);
    input.process(events);
    EXPECT_THROW(input.process_more(std::span{events}.first(1)), std::overflow_error);
    EXPECT_EQ(input.statistics().events, 256U);
    finish();
    begin(2);
    input.process({});
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange) -- Deliberate corrupt replay.
    events.front().kind = static_cast<input_kind>(255);
    const auto before = handler->events.size();
    EXPECT_THROW(input.process_more(std::span{events}.first(1)), std::invalid_argument);
    EXPECT_EQ(handler->events.size(), before);
    finish();
}
} // namespace atlas::ui::tests
