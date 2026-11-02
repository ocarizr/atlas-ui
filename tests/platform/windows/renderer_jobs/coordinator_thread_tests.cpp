/** @file @brief Application-created graphics coordination reuses a pool created on another thread.
 */
#include <atlas/jobs/job_system.hpp>
#include <atlas/renderer/passes.hpp>
#include <atlas/renderer_jobs/executor.hpp>
#include <future>
#include <gtest/gtest.h>

namespace atlas::renderer_jobs::tests
{
/// @brief Graphics services stay on their creating coordinator while persistent workers record.
TEST(renderer_coordinator, application_owns_dedicated_thread_and_shared_pool)
{
    jobs::job_system pool{2};
    executor adapter{pool.dispatcher()};
    const auto main_thread = std::this_thread::get_id();
    auto result = std::async(
        std::launch::async,
        [&]
        {
            EXPECT_NE(std::this_thread::get_id(), main_thread);
            renderer::device graphics{{renderer::adapter_preference::software}};
            renderer::resource_store resources{graphics};
            renderer::pass_renderer rendering{resources};
            renderer::pass_description pass;
            pass.color.target =
                resources.create_texture({8, 8, renderer::texture_role::color_target});
            const std::array<renderer::vertex, 3> vertices{{{{-1, -1, 0}, {1, 1, 1, 1}, {}},
                                                            {{-1, 3, 0}, {1, 1, 1, 1}, {}},
                                                            {{3, -1, 0}, {1, 1, 1, 1}, {}}}};
            const auto geometry = resources.create_buffer({sizeof(vertices)});
            resources.upload(geometry, std::as_bytes(std::span{vertices}));
            renderer::draw_packet draw;
            draw.pipeline = rendering.create_pipeline({});
            draw.vertices = geometry;
            draw.vertex_count = 3;
            pass.draws.assign(2, draw);
            const std::array passes{pass};
            resources.wait(rendering.submit(renderer::frame_plan{passes}, adapter, {1, 2, true}));
            EXPECT_EQ(rendering.last_recording().worker_batches, 2U);
            EXPECT_EQ(resources.readback(pass.color.target).size(), std::size_t{8} * 8 * 4);
            EXPECT_EQ(graphics.debug_error_count(), 0U);
        });
    result.get();
    EXPECT_EQ(pool.statistics().workers, 2U);
}
} // namespace atlas::renderer_jobs::tests
