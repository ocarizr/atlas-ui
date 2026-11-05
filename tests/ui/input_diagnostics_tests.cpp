/** @file @brief Optional input instrumentation and original failure reporting. */
#include "input_test_support.hpp"
#include <sstream>

namespace atlas::ui::tests
{
/// @brief Profiling follows the build policy and failure warnings do not replace handler errors.
TEST_F(input_fixture, CollectsOptionalScopesAndLogsFailedDispatch)
{
    diagnostics::logging_options options;
    options.asynchronous = false;
    const auto log = std::make_shared<diagnostics::logger>(options);
    const auto capture = std::make_shared<diagnostics::profiler>();
    input_session observed{tree, {}, {}, {log, capture}};
    const auto target = tree.create();
    observed.bind(target, handler);
    handler->action = [](const routed_event& event, command_buffer&)
    {
        if (event.kind == event_kind::tick)
        {
            throw std::runtime_error("diagnostic failure fixture");
        }

        return event_response{};
    };
    const auto source = tree.snapshot();
    observed.begin_frame(1, source, layout.calculate(source));
    observed.process({});
    EXPECT_THROW(observed.tick(), std::runtime_error);
    EXPECT_GE(log->statistics().written, 1);
    std::ostringstream trace;
    capture->write_chrome_trace(trace);
    for (const auto name : {"ui.input.prepare", "ui.input.process", "ui.input.callback"})
    {
        EXPECT_EQ(trace.str().find(name) != std::string::npos,
                  diagnostics::instrumentation_enabled);
    }

    EXPECT_EQ(capture->statistics().errors, 0);
}
} // namespace atlas::ui::tests
