#include <atlas/diagnostics/logging.hpp>
#include <gtest/gtest.h>

#include <future>
#include <thread>
#include <vector>

namespace atlas::diagnostics::tests
{
class counting_output : public log_output
{
  public:
    void append(std::string_view) override
    {
        ++writes;
    }
    void flush() override {}
    int writes{0};
};

TEST(logging_lifecycle_tests, bounded_overflow_drops_old_records_without_blocking_producers)
{
    class gated_output final : public counting_output
    {
      public:
        void append(std::string_view text) override
        {
            if (writes == 0)
            {
                started.set_value();
                released.wait();
            }
            counting_output::append(text);
        }
        std::promise<void> started, release;
        std::shared_future<void> released{release.get_future().share()};
    };
    auto output = std::make_shared<gated_output>();
    auto started = output->started.get_future();
    logging_options options;
    options.queue_capacity = 2;
    logger log{options, output};
    struct cleanup
    {
        std::shared_ptr<gated_output> output;
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

TEST(logging_lifecycle_tests, output_reentrancy_is_rejected_without_deadlocking)
{
    class reentrant_output final : public counting_output
    {
      public:
        void append(std::string_view text) override
        {
            EXPECT_FALSE(log->write(log_level::info, "test", "recursive"));
            EXPECT_FALSE(log->flush());
            EXPECT_THROW((void)log->statistics(), std::logic_error);
            counting_output::append(text);
        }
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

TEST(logging_lifecycle_tests, producers_do_not_wait_for_external_io_during_flush)
{
    class slow_flush final : public counting_output
    {
      public:
        void flush() override
        {
            if (first)
            {
                first = false;
                started.set_value();
                released.wait();
            }
        }
        bool first{true};
        std::promise<void> started, release;
        std::shared_future<void> released{release.get_future().share()};
    };
    auto output = std::make_shared<slow_flush>();
    auto started = output->started.get_future();
    logger log{{}, output};
    auto flushing = std::async(std::launch::async, [&] { return log.flush(); });
    EXPECT_EQ(started.wait_for(std::chrono::seconds{3}), std::future_status::ready);
    auto writing = std::async(std::launch::async,
                              [&] { return log.write(log_level::info, "test", "during flush"); });
    const auto ready = writing.wait_for(std::chrono::seconds{3});
    output->release.set_value();
    EXPECT_EQ(ready, std::future_status::ready);
    EXPECT_FALSE(writing.get());
    EXPECT_TRUE(flushing.get());
    EXPECT_EQ(log.statistics().dropped, 1U);
}
} // namespace atlas::diagnostics::tests
