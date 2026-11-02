/** @file @brief CPU jobs prepare aligned payloads before coordinator-owned GPU upload. */
#include <array>
#include <atlas/jobs_diagnostics/observer.hpp>
#include <atlas/memory/memory_resource.hpp>
#include <atlas/renderer/resources.hpp>
#include <gtest/gtest.h>
#include <sstream>

namespace atlas::renderer::tests
{
/// @brief Keep adjacent CPU write regions on separate cache lines before texture upload.
struct alignas(64) pixel_row
{
    std::array<std::byte, 256> bytes{}; ///< Sixty-four tightly packed RGBA pixels.
};

static_assert(sizeof(pixel_row) == 256);

/// @brief Input ownership is retained by the root and every attached child.
struct pixel_work
{
    std::shared_ptr<std::pmr::memory_resource> memory; ///< Outlives the PMR row allocation.
    std::shared_ptr<std::pmr::vector<pixel_row>>
        rows; ///< Exclusive disjoint writes, then immutable source bytes.
};

/// @brief One independent group of whole aligned rows.
struct row_work
{
    pixel_work input;  ///< Shared immutable ownership metadata.
    std::size_t begin; ///< First exclusively written row.
    std::size_t count; ///< Number of rows in this child.
};

/// @brief The existing application pool prepares data; no worker enters the GPU API or waits on a
/// fence.
TEST(renderer_jobs_integration_tests, prepares_with_attached_jobs_and_uploads_on_coordinator)
{
    auto memory = memory::make_resource();
    auto profile = std::make_shared<diagnostics::profiler>();
    jobs::job_system_options options;
    options.worker_count = 2;
    options.scratch.upstream = memory.get();
    options.observer = jobs_diagnostics::make_observer({}, profile);
    jobs::job_system pool{options};
    pixel_work input{memory, std::make_shared<std::pmr::vector<pixel_row>>(memory.get())};
    input.rows->resize(8);
    auto prepared = pool.dispatcher().run(
        jobs::job_label{"prepare texture", "renderer.cpu"}, input,
        [](jobs::job_context& parent, pixel_work& state)
        {
            for (std::size_t begin = 0; begin < 8; begin += 4)
            {
                parent.spawn("prepare rows", row_work{state, begin, 4},
                             [](jobs::job_context& context, row_work& work)
                             {
                                 auto rows =
                                     std::span{*work.input.rows}.subspan(work.begin, work.count);
                                 auto writing = context.access(jobs::write_access(rows));
                                 for (std::size_t row = 0; row < rows.size(); ++row)
                                 {
                                     rows[row].bytes.fill(static_cast<std::byte>(work.begin + row));
                                 }
                             });
            }
        });
    prepared.wait();
    device graphics{{adapter_preference::software}, memory, {{}, profile}};
    resource_store resources{graphics};
    const auto texture = resources.create_texture({64, 8});
    const auto bytes = std::as_bytes(std::span{*input.rows});
    const auto uploaded = resources.upload(texture, bytes);
    resources.wait(uploaded);
    const auto result = resources.readback(texture);
    ASSERT_EQ(result.size(), 64U * 8U * 4U);
    EXPECT_TRUE(std::equal(result.begin(), result.end(), bytes.begin(), bytes.end()));
    resources.release(texture);
    resources.collect();
    EXPECT_EQ(resources.statistics().allocation_bytes, 0U);
    pool.shutdown();
    EXPECT_EQ(graphics.debug_error_count(), 0U);
    std::ostringstream trace;
    profile->write_chrome_trace(trace);
    EXPECT_EQ(trace.str().find("resource.upload") != std::string::npos,
              diagnostics::instrumentation_enabled);
}
} // namespace atlas::renderer::tests
