/**
 * @file
 * @brief Behavioral coverage for logging lifecycle with isolated scenario ownership and cleanup.
 */

#include <atlas/diagnostics/logging.hpp>
#include <gtest/gtest.h>

#include <future>
#include <thread>
#include <vector>

namespace atlas::diagnostics::tests
{
/// @brief Scenario state for counting output, isolated per test and released through RAII.
class counting_output : public log_output
{
  public:
    /// @brief Record or deliberately fail output as selected by this isolated logging scenario.
    void append(std::string_view) override
    {
        ++writes;
    }

    /// @brief Acknowledge or deliberately fail the scenario's controlled output flush.
    void flush() override
    {
    }

    /// @brief Scenario-owned writes used to observe behavior and cleanup without sharing mutable
    /// state between tests.
    int writes{0};
};

/// @brief Verify bounded overflow drops old records without blocking producers.
TEST(logging_lifecycle_tests, bounded_overflow_drops_old_records_without_blocking_producers)
{
    /// @brief Scenario state for gated output, isolated per test and released through RAII.
    class gated_output final : public counting_output
    {
      public:
        /// @brief Record or deliberately fail output as selected by this isolated logging scenario.
        void append(std::string_view text) override
        {
            if (writes == 0)
            {
                started.set_value();
                released.wait();
            }

            counting_output::append(text);
        }

        /// @brief Scenario-owned started used to observe behavior and cleanup without sharing
        /// mutable state between tests.
        std::promise<void> started, release;
        /// @brief Scenario-owned released used to observe behavior and cleanup without sharing
        /// mutable state between tests.
        std::shared_future<void> released{release.get_future().share()};
    };

    auto output = std::make_shared<gated_output>();
    auto started = output->started.get_future();
    logging_options options;
    options.queue_capacity = 2;
    logger log{options, output};

    /// @brief Scenario state for cleanup, isolated per test and released through RAII.
    struct cleanup
    {
        /// @brief Scenario-owned output used to observe behavior and cleanup without sharing
        /// mutable state between tests.
        std::shared_ptr<gated_output> output;

        /// @brief Release cleanup resources and complete its scoped cleanup.
        ~cleanup()
        {
            try
            {
                output->release.set_value();
            }
            catch (...)
            {
                std::terminate();
            }
        }
    };

    {
        cleanup release{output};
        EXPECT_TRUE(log.write(log_level::info, "test", "first"));
        EXPECT_EQ(started.wait_for(std::chrono::seconds{3}), std::future_status::ready);
        for (int index = 0; index < 20; ++index)
        {
            EXPECT_TRUE(log.write(log_level::info, "test", "queued"));
        }

        EXPECT_EQ(log.statistics().dropped, 18U);
    }
    EXPECT_TRUE(log.flush());
    EXPECT_EQ(output->writes, 3);
    EXPECT_EQ(log.statistics().written, 3U);
}

/// @brief Verify destruction drains concurrent producers and joins the backend.
TEST(logging_lifecycle_tests, destruction_drains_concurrent_producers_and_joins_the_backend)
{
    auto output = std::make_shared<counting_output>();
    {
        logger log{{}, output};
        std::vector<std::jthread> producers;
        producers.reserve(4);
        for (int thread = 0; thread < 4; ++thread)
        {
            producers.emplace_back(
                [&]
                {
                    for (int item = 0; item < 32; ++item)
                    {
                        EXPECT_TRUE(log.write(log_level::info, "test", "message"));
                    }
                });
        }

        producers.clear();
    }
    EXPECT_EQ(output->writes, 128);
}

/// @brief Verify output reentrancy is rejected without deadlocking.
TEST(logging_lifecycle_tests, output_reentrancy_is_rejected_without_deadlocking)
{
    /// @brief Scenario state for reentrant output, isolated per test and released through RAII.
    class reentrant_output final : public counting_output
    {
      public:
        /// @brief Record or deliberately fail output as selected by this isolated logging scenario.
        void append(std::string_view text) override
        {
            EXPECT_FALSE(log->write(log_level::info, "test", "recursive"));
            EXPECT_FALSE(log->flush());
            EXPECT_THROW((void)log->statistics(), std::logic_error);
            counting_output::append(text);
        }

        /// @brief Scenario-owned log used to observe behavior and cleanup without sharing mutable
        /// state between tests.
        logger* log{nullptr};
    };

    auto output = std::make_shared<reentrant_output>();
    logger log{{}, output};
    output->log = &log;
    log.write(log_level::info, "test", "outer");
    (void)log.flush();
    EXPECT_EQ(output->writes, 1);
    EXPECT_EQ(log.statistics().errors, 2U);
}

/// @brief Verify record and category limits fail observably.
TEST(logging_lifecycle_tests, record_and_category_limits_fail_observably)
{
    logging_options options;
    options.max_message_bytes = 4;
    options.max_categories = 1;
    logger log{options, std::make_shared<counting_output>()};
    EXPECT_FALSE(log.write(log_level::info, "test", "large"));
    EXPECT_TRUE(log.write(log_level::info, "test", "ok"));
    EXPECT_FALSE(log.write(log_level::info, "other", "ok"));
    EXPECT_EQ(log.statistics().errors, 2U);
}

/// @brief Verify producers do not wait for external io during flush.
TEST(logging_lifecycle_tests, producers_do_not_wait_for_external_io_during_flush)
{
    /// @brief Scenario state for slow flush, isolated per test and released through RAII.
    class slow_flush final : public counting_output
    {
      public:
        /// @brief Acknowledge or deliberately fail the scenario's controlled output flush.
        void flush() override
        {
            if (first)
            {
                first = false;
                started.set_value();
                released.wait();
            }
        }

        /// @brief Scenario-owned first used to observe behavior and cleanup without sharing mutable
        /// state between tests.
        bool first{true};
        /// @brief Scenario-owned started used to observe behavior and cleanup without sharing
        /// mutable state between tests.
        std::promise<void> started, release;
        /// @brief Scenario-owned released used to observe behavior and cleanup without sharing
        /// mutable state between tests.
        std::shared_future<void> released{release.get_future().share()};
    };

    auto output = std::make_shared<slow_flush>();
    auto started = output->started.get_future();
    logger log{{}, output};
    auto flushing = std::async(std::launch::async,
                               [&]
                               {
                                   return log.flush();
                               });
    EXPECT_EQ(started.wait_for(std::chrono::seconds{3}), std::future_status::ready);
    auto writing = std::async(std::launch::async,
                              [&]
                              {
                                  return log.write(log_level::info, "test", "during flush");
                              });
    const auto ready = writing.wait_for(std::chrono::seconds{3});
    output->release.set_value();
    EXPECT_EQ(ready, std::future_status::ready);
    EXPECT_FALSE(writing.get());
    EXPECT_TRUE(flushing.get());
    EXPECT_EQ(log.statistics().dropped, 1U);
}
} // namespace atlas::diagnostics::tests
