/**
 * @file
 * @brief Behavioral coverage for range sanitizer with isolated scenario ownership and cleanup.
 */

#include "job_system_fixture.hpp"

#include <array>

namespace atlas::jobs::tests
{
/// @brief Verify parallel ranges automatically declare their chunk access.
TEST_F(job_system_fixture, parallel_ranges_automatically_declare_their_chunk_access)
{
    /// @brief Scenario state for input, isolated per test and released through RAII.
    struct input
    {
        /// @brief Scenario-owned values used to observe behavior and cleanup without sharing
        /// mutable state between tests.
        std::span<int> values;
        /// @brief Scenario-owned gate used to observe behavior and cleanup without sharing mutable
        /// state between tests.
        signal_gate* gate;
        /// @brief Scenario-owned started used to observe behavior and cleanup without sharing
        /// mutable state between tests.
        std::shared_ptr<std::promise<void>> started;
    };

    std::array<int, 1> values{};
    auto started = std::make_shared<std::promise<void>>();
    auto ready = started->get_future();
    auto root = system.dispatcher().run("held", input{values, &gate, started},
                                        [](job_context& context, input& data)
                                        {
                                            auto guard = context.access(write_access(data.values));
                                            data.started->set_value();
                                            data.gate->wait();
                                        });
    EXPECT_EQ(ready.wait_for(std::chrono::seconds{3}), std::future_status::ready);
    auto range = system.dispatcher().parallel_for("range", std::span<int>{values}, 1,
                                                  [](job_context&, std::span<int> chunk)
                                                  {
                                                      ++chunk.front();
                                                  });
    const auto enabled = system.sanitizer_report().enabled;
    if (enabled)
    {
        EXPECT_THROW(range.wait(), job_error);
    }
    else
    {
        range.wait();
    }

    gate.open();
    root.wait();
    EXPECT_EQ(values.front(), enabled ? 0 : 1);
}
} // namespace atlas::jobs::tests
