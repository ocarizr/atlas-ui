/** @file @brief Application-owned memory policy shared by independent job and streaming libraries.
 */
#include "temporary_assets.hpp"
#include <atlas/jobs/job_system.hpp>
#include <atlas/memory/memory_resource.hpp>
#include <atlas/streaming/asset_streamer.hpp>
#include <gtest/gtest.h>

namespace atlas::memory::tests
{
/// @brief Job scratch uses the injected allocator and releases blocks after task completion.
TEST(memory_consumer_tests, job_scopes_return_upstream_blocks)
{
    const auto resource = make_resource({true});
    jobs::job_system_options options;
    options.worker_count = 3;
    options.scratch.upstream = resource.get();
    jobs::job_system pool{options};
    std::vector<jobs::task_handle> handles;
    handles.reserve(16);
    for (int index = 0; index < 16; ++index)
    {
        handles.push_back(pool.dispatcher().run("memory.scratch",
                                                [](jobs::job_context& context)
                                                {
                                                    std::pmr::vector<std::uint64_t> values{
                                                        &context.scratch()};
                                                    values.resize(1024, 25);
                                                    EXPECT_EQ(values.back(), 25);
                                                }));
    }

    for (const auto& handle : handles)
    {
        handle.wait();
    }

    pool.shutdown();
    EXPECT_GT(resource->statistics().allocation_calls, 0);
    EXPECT_EQ(resource->statistics().live_requested_bytes, 0);
    EXPECT_EQ(resource->statistics().allocation_calls, resource->statistics().deallocation_calls);
}

/// @brief A loose-file result pins the same application-selected allocator after service shutdown.
TEST(memory_consumer_tests, streamed_buffers_retain_allocator_after_services_are_destroyed)
{
    streaming::tests::temporary_assets assets;
    assets.write();
    auto resource = make_resource({true});
    const std::weak_ptr<memory_resource> weak = resource;
    std::shared_ptr<const streaming::asset_bytes> retained;
    {
        jobs::job_system_options options;
        options.worker_count = 2;
        options.scratch.upstream = resource.get();
        jobs::job_system pool{options};
        streaming::asset_streamer streamer{
            pool, {{"data", streaming::loose_file_asset{assets.path()}}}, {}, resource};
        streamer
            .read({"data", 0, 4},
                  [&retained](streaming::read_result result)
                  {
                      retained = std::get<std::shared_ptr<const streaming::asset_bytes>>(
                          std::move(result));
                  })
            .get();
        streamer.shutdown();
        pool.shutdown();
    }
    ASSERT_TRUE(retained);
    EXPECT_EQ(*retained, (streaming::asset_bytes{'A', 'B', 'C', 'D'}));
    EXPECT_GE(resource->statistics().live_requested_bytes, 4);
    resource.reset();
    EXPECT_FALSE(weak.expired());
    retained.reset();
    EXPECT_TRUE(weak.expired());
}

/// @brief Cancellation must release admission without requesting a payload allocation.
TEST(memory_consumer_tests, cancelled_reads_do_not_allocate_payloads)
{
    const auto resource = make_resource({true});
    jobs::job_system pool{1};
    streaming::asset_streamer streamer{
        pool, {{"data", streaming::loose_file_asset{"does-not-exist"}}}, {}, resource};
    std::stop_source stop;
    stop.request_stop();
    streamer
        .read({"data", 0, 4},
              [](streaming::read_result result)
              {
                  EXPECT_THROW(std::rethrow_exception(std::get<std::exception_ptr>(result)),
                               jobs::job_cancelled);
              },
              {jobs::job_priority::background, stop.get_token()})
        .get();
    streamer.shutdown();
    EXPECT_EQ(resource->statistics().allocation_calls, 0);
    EXPECT_EQ(streamer.statistics().resident_bytes, 0);
}

/// @brief Exceptions in the consumer cannot strand its prepared output or shared allocator bytes.
TEST(memory_consumer_tests, callback_failure_releases_shared_resource_payload)
{
    const auto resource = make_resource({true});
    const auto input = std::make_shared<const streaming::asset_bytes>(256, std::uint8_t{25});
    jobs::job_system pool{2};
    streaming::asset_streamer streamer{
        pool, {{"data", streaming::memory_asset{input}}}, {}, resource};
    auto finished = streamer.read({"data", 0, input->size()},
                                  [](const streaming::read_result&)
                                  {
                                      throw std::runtime_error{"controlled callback failure"};
                                  });
    EXPECT_THROW(finished.get(), std::runtime_error);
    streamer.shutdown();
    EXPECT_GT(resource->statistics().allocation_calls, 0);
    EXPECT_EQ(resource->statistics().live_requested_bytes, 0);
}
} // namespace atlas::memory::tests
