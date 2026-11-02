/** @file @brief Conflict diagnostics remain bounded, optional and unable to mutate active sealing.
 */
#include <atlas/ui/commands.hpp>
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
} // namespace atlas::ui::tests
