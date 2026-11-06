/** @file @brief PMR lifetime, bounded queue admission and allocation-free numeric update metadata.
 */
#include "counting_resource.hpp"
#include <atlas/mvvm/view_model.hpp>
#include <gtest/gtest.h>

namespace atlas::mvvm::tests
{
/// @brief Numeric columns/queues/observer indexes reserve through the shared project resource.
TEST(mvvm_resources, NumericDispatchDoesNotAllocatePmrAfterSetupAndReleasesAllStorage)
{
    auto memory = std::make_shared<benchmarks::counting_resource>();
    const auto schema = compile({"model", {{"count", std::int64_t{0}}, {"enabled", false}}});
    {
        view_model model{schema, {}, memory};
        const auto allocations = memory->allocations;
        memory->reject = true;
        for (std::uint64_t frame = 1; frame <= 8; ++frame)
        {
            model.begin_frame(frame);
            model.set(schema.field("count"), static_cast<std::int64_t>(frame));
            model.set(schema.field("enabled"), frame % 2 == 0);
            model.flush();
        }

        EXPECT_EQ(memory->allocations, allocations);
        EXPECT_EQ(model.integer(schema.field("count")), 8);
    }
    EXPECT_EQ(memory->live_bytes, 0);
}

/// @brief Constructor allocation failure cannot retain partially allocated instance storage.
TEST(mvvm_resources, FailedConstructionDoesNotLeak)
{
    auto memory = std::make_shared<benchmarks::counting_resource>();
    memory->reject = true;
    const auto schema = compile({"model", {{"x", false}}});
    EXPECT_THROW((view_model{schema, {}, memory}), std::bad_alloc);
    EXPECT_EQ(memory->live_bytes, 0);
}

/// @brief Rejected commands preserve earlier work and do not consume an extra admission slot.
TEST(mvvm_resources, QueueAndPayloadBoundsFailExplicitly)
{
    const auto schema = compile({"model", {{"a", false}, {"b", false}, {"text", std::string{}}}});
    view_model model{schema, {.max_commands = 1, .max_arguments = 1, .max_payload_bytes = 2}};
    model.begin_frame(1);
    EXPECT_THROW(model.set(schema.field("text"), std::string{"long"}), std::overflow_error);
    EXPECT_EQ(model.inspect().commands, 0);
    model.set(schema.field("a"), true);
    EXPECT_THROW(model.set(schema.field("b"), true), std::overflow_error);
    model.flush();
    EXPECT_TRUE(model.boolean(schema.field("a")));
    EXPECT_FALSE(model.boolean(schema.field("b")));
}

/// @brief A failing variable-length commit cannot corrupt the previous string or leak its resource.
TEST(mvvm_resources, TextAllocationFailurePreservesCommittedStateAndSupportsRecovery)
{
    auto memory = std::make_shared<benchmarks::counting_resource>();
    const auto schema = compile({"model", {{"title", std::string{"old"}}}});
    {
        view_model model{schema, {}, memory};
        const auto title = schema.field("title");
        model.begin_frame(1);
        model.set(title, std::string(512, 'x'));
        memory->reject = true;
        EXPECT_THROW(model.flush(), std::bad_alloc);
        EXPECT_EQ(model.text(title), "old");
        EXPECT_TRUE(model.inspect().failed);
        memory->reject = false;
        model.reset();
        model.begin_frame(2);
        model.set(title, std::string{"new"});
        model.flush();
        EXPECT_EQ(model.text(title), "new");
    }
    EXPECT_EQ(memory->live_bytes, 0);
}
} // namespace atlas::mvvm::tests
