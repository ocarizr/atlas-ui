/** @file @brief Concurrent allocation and cross-thread release contracts for shared PMR resources.
 */
#include <algorithm>
#include <array>
#include <atlas/memory/memory_resource.hpp>
#include <barrier>
#include <future>
#include <gtest/gtest.h>
#include <thread>

namespace atlas::memory::tests
{
/// @brief Each caller can allocate concurrently without requiring user-managed thread heaps.
TEST(memory_concurrency_tests, shares_resource_across_concurrent_owners)
{
    const auto resource = make_resource({true});
    std::barrier start{4};
    std::array<std::jthread, 4> workers;
    std::size_t started{0};
    try
    {
        for (std::size_t worker = 0; worker < workers.size(); ++worker)
        {
            workers[worker] = std::jthread{[&, worker]
                                           {
                                               start.arrive_and_wait();
                                               for (std::size_t index = 0; index < 1000; ++index)
                                               {
                                                   const auto size = 64 + (index + worker) % 512;
                                                   auto* pointer = resource->allocate(size, 64);
                                                   std::fill_n(static_cast<std::byte*>(pointer),
                                                               size, std::byte{0x25});
                                                   resource->deallocate(pointer, size, 64);
                                               }
                                           }};
            ++started;
        }
    }
    catch (...)
    {
        for (std::size_t missing = started; missing < workers.size(); ++missing)
        {
            start.arrive_and_drop();
        }

        throw;
    }

    for (auto& worker : workers)
    {
        worker.join();
    }

    const auto report = resource->statistics();
    EXPECT_EQ(report.allocation_calls, 4000);
    EXPECT_EQ(report.deallocation_calls, 4000);
    EXPECT_EQ(report.live_requested_bytes, 0);
}

/// @brief Ownership can cross threads without freeing through a different allocator.
TEST(memory_concurrency_tests, releases_worker_allocation_on_coordinator)
{
    const auto resource = make_resource({true});
    std::promise<void*> ready;
    auto future = ready.get_future();
    std::jthread worker{[&]
                        {
                            ready.set_value(resource->allocate(4096, 256));
                        }};
    auto* pointer = future.get();
    worker.join();
    EXPECT_EQ(resource->statistics().live_requested_bytes, 4096);
    resource->deallocate(pointer, 4096, 256);
    EXPECT_EQ(resource->statistics().live_requested_bytes, 0);
}
} // namespace atlas::memory::tests
