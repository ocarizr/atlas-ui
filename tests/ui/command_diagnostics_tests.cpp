/** @file @brief Conflict diagnostics remain bounded, optional and unable to mutate active sealing.
 */
#include <atlas/jobs/job_system.hpp>
#include <atlas/ui/commands.hpp>
#include <atlas/ui/element_tree.hpp>
#include <atlas/ui/frame_executor.hpp>
#include <atlas/ui/layout_engine.hpp>
#include <atlas/ui/paint_engine.hpp>
#include <functional>
#include <gtest/gtest.h>
#include <sstream>

namespace atlas::ui::tests
{
/// @brief Synchronous test output owns copied text; never reenter the logger itself.
class observed_output final : public diagnostics::log_output
{
  public:
    void append(std::string_view message) override
    {
        if (fail)
        {
            throw std::runtime_error("controlled diagnostic output failure");
        }

        text.append(message);
        if (observe)
        {
            observe();
        }
    } ///< Optional callback probes the UI producer, not the logger.

    void flush() override
    {
    } ///< No external resources need flushing.

    std::string text;              ///< Owned diagnostic observations.
    std::function<void()> observe; ///< Optional same-thread reentrancy probe.
    bool fail = false;             ///< Isolated failure injection.
};

/// @brief Each fact owns its sinks and profiling state; no global logging is configured.
class command_diagnostics_test : public ::testing::Test
{
  protected:
    std::shared_ptr<observed_output> output = std::make_shared<observed_output>(); ///< Copied logs.
    std::shared_ptr<diagnostics::logger> log = std::make_shared<diagnostics::logger>(
        options(), output); ///< Synchronous deterministic delivery for these tests.
    std::shared_ptr<diagnostics::profiler> capture =
        std::make_shared<diagnostics::profiler>(); ///< CPU capture.

    static diagnostics::logging_options options()
    {
        diagnostics::logging_options policy;
        policy.asynchronous = false;
        return policy;
    } ///< Avoid backend scheduling in assertions about warning counts.
};

/// @brief Summary/detail limits do not discard the full structured conflict records.
TEST_F(command_diagnostics_test, bounds_warnings_and_retains_provenance)
{
    command_buffer buffer{{8, 2, 1}, {}, {log, capture}};
    buffer.begin_frame(17);
    for (std::uint32_t index = 0; index < 3; ++index)
    {
        buffer.set({index, 1}, property::width, 1.f, 10);
        buffer.set({index, 1}, property::width, 2.f, 20);
    }

    const auto batch = buffer.seal();
    EXPECT_EQ(batch.conflicts().size(), 3U);
    EXPECT_EQ(log->statistics().written, 2U);
    EXPECT_NE(output->text.find("frame=17 batch=1 keys=3"), std::string::npos);
    EXPECT_NE(output->text.find("property=width producer=10 sequence=1"), std::string::npos);
    EXPECT_NE(output->text.find("producer=20 sequence=2"), std::string::npos);
    std::ostringstream trace;
    capture->write_chrome_trace(trace);
    EXPECT_EQ(trace.str().find("ui.commands.seal") != std::string::npos,
              diagnostics::instrumentation_enabled);
    EXPECT_EQ(capture->statistics().errors, 0U);
}

/// @brief Ordinary unique writes do not create per-property warning noise.
TEST_F(command_diagnostics_test, unique_writes_do_not_warn)
{
    command_buffer buffer{{8, 2, 0}, {}, {log, {}}};
    buffer.begin_frame(1);
    buffer.set({0, 1}, property::visible, true, 1);
    EXPECT_TRUE(buffer.seal().conflicts().empty());
    EXPECT_EQ(log->statistics().written, 0U);
}

/// @brief Output failure cannot invalidate a successfully prepared immutable batch.
TEST_F(command_diagnostics_test, logging_failure_does_not_change_command_results)
{
    output->fail = true;
    command_buffer buffer{{8, 2, 0}, {}, {log, {}}};
    buffer.begin_frame(1);
    buffer.set({0, 1}, property::width, 1.f, 1);
    buffer.set({0, 1}, property::width, 2.f, 2);
    const auto batch = buffer.seal();
    ASSERT_EQ(batch.writes().size(), 1U);
    EXPECT_EQ(batch.writes()[0].value, property_value{2.f});
    EXPECT_GT(log->statistics().errors, 0U);
    EXPECT_EQ(buffer.pending(), 0U);
}

/// @brief Synchronous observers cannot append/reset while a sealing transaction is active.
TEST_F(command_diagnostics_test, rejects_diagnostic_reentrancy_without_partial_mutation)
{
    command_buffer buffer{{8, 2, 0}, {}, {log, {}}};
    buffer.begin_frame(1);
    buffer.set({0, 1}, property::width, 1.f, 1);
    buffer.set({0, 1}, property::width, 2.f, 2);
    bool observed = false;
    output->observe = [&]
    {
        observed = true;
        EXPECT_THROW(buffer.set({0, 1}, property::width, 3.f, 3), std::logic_error);
        EXPECT_THROW(buffer.begin_frame(2), std::logic_error);
    };
    const auto batch = buffer.seal();
    output->observe = {};
    EXPECT_TRUE(observed);
    EXPECT_EQ(buffer.pending(), 0U);
    EXPECT_EQ(batch.writes()[0].value, property_value{2.f});
    buffer.begin_frame(2);
}

/// @brief Failed publication is observable but its sink cannot reenter tree or bound commands.
TEST_F(command_diagnostics_test, tree_failure_logs_and_rejects_reentrant_mutation)
{
    element_tree tree{{4}, {}, {log, capture}};
    const auto root = tree.create();
    auto& commands = tree.commands();
    commands.begin_frame(1);
    const auto batch = commands.seal();
    tree.apply(batch);
    bool observed = false;
    output->observe = [&]
    {
        observed = true;
        EXPECT_THROW(tree.create(), std::logic_error);
        EXPECT_THROW(commands.set(root, property::width, 1.f, 1), std::logic_error);
    };
    EXPECT_THROW(tree.apply(batch), std::invalid_argument);
    output->observe = {};
    EXPECT_TRUE(observed);
    EXPECT_EQ(tree.size(), 1U);
    EXPECT_EQ(commands.pending(), 0U);
    std::ostringstream trace;
    capture->write_chrome_trace(trace);
    EXPECT_EQ(trace.str().find("ui.tree.apply") != std::string::npos,
              diagnostics::instrumentation_enabled);
}

/// @brief Phase names remain owned/stable while callbacks mutate their original strings.
TEST_F(command_diagnostics_test, frame_scopes_own_names_and_reject_logging_reentrancy)
{
    jobs::job_system pool{1};
    frame_executor executor{pool.dispatcher(), {}, {}, {log, capture}};
    executor.begin_frame(1);
    std::string label = "original_dispatch_name";
    executor.dispatch(label,
                      [&]
                      {
                          label.assign(256, 'x');
                      });
    bool observed = false;
    output->observe = [&]
    {
        observed = true;
        EXPECT_THROW(executor.dispatch("reentrant",
                                       []
                                       {
                                       }),
                     std::logic_error);
        EXPECT_THROW(executor.statistics(), std::logic_error);
    };
    EXPECT_THROW(executor.dispatch("failed_dispatch",
                                   []
                                   {
                                       throw std::domain_error("controlled");
                                   }),
                 std::domain_error);
    output->observe = {};
    EXPECT_TRUE(observed);
    std::ostringstream trace;
    capture->write_chrome_trace(trace);
    EXPECT_EQ(trace.str().find("original_dispatch_name") != std::string::npos,
              diagnostics::instrumentation_enabled);
}

/// @brief Layout scopes/logs remain optional and synchronous outputs cannot reenter the engine.
TEST_F(command_diagnostics_test, layout_capture_and_failure_logging_keep_the_owner_guard)
{
    jobs::job_system pool{1};
    element_tree tree;
    tree.create();
    layout_engine engine{pool.dispatcher(), {}, {}, {log, capture}};
    engine.update(tree, {0, 0, 100, 50});
    bool observed = false;
    output->observe = [&]
    {
        observed = true;
        EXPECT_THROW(engine.statistics(), std::logic_error);
    };
    EXPECT_THROW(engine.calculate({}), std::invalid_argument);
    output->observe = {};
    EXPECT_TRUE(observed);
    std::ostringstream trace;
    capture->write_chrome_trace(trace);
    EXPECT_EQ(trace.str().find("ui.layout.measure") != std::string::npos,
              diagnostics::instrumentation_enabled);
    EXPECT_EQ(trace.str().find("ui.layout.arrange") != std::string::npos,
              diagnostics::instrumentation_enabled);
}

/// @brief New policy/slot/visual operations follow the same optional trace and guarded failure
/// rules.
TEST_F(command_diagnostics_test, canvas_operations_trace_and_reject_diagnostic_reentry)
{
    element_tree tree{{}, {}, {log, capture}};
    const auto root = tree.create();
    const auto child = tree.create(root);
    tree.set_panel(root, canvas_layout{});
    canvas_slot slot;
    slot.horizontal.offset_min = 10;
    tree.set_canvas_slot(child, slot);
    tree.set_visual(child, {{0, 0}});
    bool observed = false;
    output->observe = [&]
    {
        observed = true;
        EXPECT_THROW(tree.panel(root), std::logic_error);
    };
    slot.horizontal.pivot = -1;
    EXPECT_THROW(tree.set_canvas_slot(child, slot), std::invalid_argument);
    output->observe = {};
    EXPECT_TRUE(observed);
    std::ostringstream trace;
    capture->write_chrome_trace(trace);
    for (const auto* name : {"ui.tree.set_panel", "ui.tree.set_canvas_slot", "ui.tree.set_visual"})
    {
        EXPECT_EQ(trace.str().find(name) != std::string::npos,
                  diagnostics::instrumentation_enabled);
    }
}

/// @brief Stack metadata follows the shared profiling policy and diagnostic callbacks cannot
/// reenter mutation.
TEST_F(command_diagnostics_test, stack_slot_scopes_and_failure_logging_remain_guarded)
{
    element_tree tree{{}, {}, {log, capture}};
    const auto root = tree.create();
    tree.set_panel(root, vertical_layout{});
    const auto child = tree.create(root);
    tree.set_stack_slot(child, {-1, 2, 1});
    bool observed = false;
    output->observe = [&]
    {
        observed = true;
        EXPECT_THROW(tree.stack_slot_for(child), std::logic_error);
    };
    EXPECT_THROW(tree.set_stack_slot(child, {0, -1, 1}), std::invalid_argument);
    output->observe = {};
    EXPECT_TRUE(observed);
    std::ostringstream trace;
    capture->write_chrome_trace(trace);
    EXPECT_EQ(trace.str().find("ui.tree.set_stack_slot") != std::string::npos,
              diagnostics::instrumentation_enabled);
}

/// @brief Dependency authoring keeps diagnostics optional and rejects synchronous callback reentry.
TEST_F(command_diagnostics_test, boundary_scopes_and_failure_logging_remain_guarded)
{
    element_tree tree{{}, {}, {log, capture}};
    const auto root = tree.create();
    tree.set_panel(root, canvas_layout{});
    const auto source = tree.create(root);
    const auto target = tree.create(root);
    tree.set_boundaries(target, {{}, boundary_relation{source}});
    bool observed = false;
    output->observe = [&]
    {
        observed = true;
        EXPECT_THROW(tree.boundaries(target), std::logic_error);
    };
    EXPECT_THROW(tree.set_boundaries(source, {{}, boundary_relation{target}}),
                 std::invalid_argument);
    output->observe = {};
    EXPECT_TRUE(observed);
    std::ostringstream trace;
    capture->write_chrome_trace(trace);
    EXPECT_EQ(trace.str().find("ui.tree.set_boundaries") != std::string::npos,
              diagnostics::instrumentation_enabled);
}

/// @brief Subtree phases expose preparation, workers, join and parent reduction under the policy.
TEST_F(command_diagnostics_test, subtree_layout_scopes_follow_optional_profiling_policy)
{
    jobs::job_system pool{2};
    element_tree tree;
    const auto root = tree.create();
    for (int index = 0; index < 4; ++index)
    {
        const auto panel = tree.create(root);
        tree.create(panel);
        tree.create(panel);
    }

    layout_engine engine{pool.dispatcher(), {64, 1, 4, 1}, {}, {log, capture}};
    engine.update(tree, {0, 0, 100, 100});
    std::ostringstream trace;
    capture->write_chrome_trace(trace);
    for (const auto* name : {"ui.layout.prepare", "ui.layout.plan", "ui.layout.subtree",
                             "ui.layout.join", "ui.layout.measure.parents", "ui.layout.arrange"})
    {
        EXPECT_EQ(trace.str().find(name) != std::string::npos,
                  diagnostics::instrumentation_enabled);
    }
}

/// @brief Joined paint scopes obey compile-time profiling policy without changing results.
TEST_F(command_diagnostics_test, paint_profiles_order_clipping_tessellation_and_join)
{
    jobs::job_system workers{2};
    element_tree tree;
    for (unsigned i = 0; i < 8; ++i)
    {
        const auto root = tree.create();
        paint_style style;
        style.fill = true;
        tree.set_paint(root, style);
    }

    tree.set_viewport({0, 0, 80, 60});
    layout_engine layout{workers.dispatcher()};
    const auto source = tree.snapshot();
    const auto geometry = layout.calculate(source);
    paint_engine paint{
        workers.dispatcher(), {4096, 1, 4, paint_execution::chunks}, {}, {log, capture}};
    EXPECT_EQ(paint.calculate(source, geometry).quads().size(), 8);
    std::ostringstream trace;
    capture->write_chrome_trace(trace);
    for (const auto* name : {"ui.paint.calculate", "ui.paint.order_clip", "ui.paint.tessellate",
                             "ui.paint.chunk", "ui.paint.join"})
    {
        EXPECT_EQ(trace.str().find(name) != std::string::npos,
                  diagnostics::instrumentation_enabled);
    }

    EXPECT_EQ(capture->statistics().errors, 0);
}
} // namespace atlas::ui::tests
