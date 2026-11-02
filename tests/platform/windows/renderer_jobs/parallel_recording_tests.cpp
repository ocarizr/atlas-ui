/** @file @brief Parallel D3D12 recording preserves sequential pixels, ownership and failure
 * atomicity. */
#include "resource_test_support.hpp"
#include <atlas/jobs/job_system.hpp>
#include <atlas/renderer/passes.hpp>
#include <atlas/renderer_jobs/executor.hpp>
#include <functional>
#include <sstream>

namespace atlas::renderer_jobs::tests
{
/// @brief Synchronously complete one accepted task and omit the rest for contract validation.
class incomplete_executor final : public renderer::recording_executor
{
  public:
    void execute(std::span<renderer::recording_task* const> tasks) override
    {
        tasks.front()->run();
    } ///< Returns with omitted work but no outstanding threads.
};

/// @brief Intentionally record in reverse CPU order while joining every operation.
class reverse_executor final : public renderer::recording_executor
{
  public:
    void execute(std::span<renderer::recording_task* const> tasks) override
    {
        for (auto iterator = tasks.rbegin(); iterator != tasks.rend(); ++iterator)
        {
            (*iterator)->run();
        }
    } ///< The GPU must still receive original frame order.
};

/// @brief Simulate coordinator callbacks pumped during an executor's joined wait.
class callback_executor final : public renderer::recording_executor
{
  public:
    std::function<void()> callback; ///< Coordinator callback before accepted work is joined.

    void execute(std::span<renderer::recording_task* const> tasks) override
    {
        callback();
        for (auto* task : tasks)
        {
            task->run();
        }
    } ///< No tasks escape this synchronous scope.
};

/// @brief One application pool and one GPU coordinator per case.
class parallel_recording_test : public ::testing::Test
{
  protected:
    jobs::job_system pool{2};            ///< Existing application-owned workers.
    executor adapter{pool.dispatcher()}; ///< Non-owning bridge.
    std::shared_ptr<diagnostics::profiler> profile =
        std::make_shared<diagnostics::profiler>(); ///< Bounded inherited worker capture.
    renderer::device graphics{{renderer::adapter_preference::software},
                              {},
                              {{}, profile}};     ///< WARP validation with optional worker capture.
    renderer::resource_store resources{graphics}; ///< Fence-protected resources.
    renderer::pass_renderer renderer{resources};  ///< Single coordinator executor.
    renderer::texture_handle target =
        resources.create_texture({16, 16, renderer::texture_role::color_target}); ///< Output image.

    /// @brief Build an order-sensitive overdraw workload over one pass.
    renderer::pass_description description()
    {
        const std::array<renderer::vertex, 3> vertices{{{{-1, -1, 0}, {1, 1, 1, 1}, {}},
                                                        {{-1, 3, 0}, {1, 1, 1, 1}, {}},
                                                        {{3, -1, 0}, {1, 1, 1, 1}, {}}}};
        const auto geometry = resources.create_buffer({sizeof(vertices)});
        resources.upload(geometry, std::as_bytes(std::span{vertices}));
        renderer::pipeline_description pipeline;
        pipeline.blend = renderer::blend_mode::straight_alpha;
        const auto state = renderer.create_pipeline(pipeline);
        renderer::pass_description pass;
        pass.color.target = target;
        for (unsigned index = 0; index < 32; ++index)
        {
            renderer::draw_packet draw;
            draw.pipeline = state;
            draw.vertices = geometry;
            draw.vertex_count = 3;
            draw.tint = index % 2 == 0 ? std::array<float, 4>{1, 0, 0, 0.2F}
                                       : std::array<float, 4>{0, 0, 1, 0.2F};
            pass.draws.push_back(draw);
        }

        return pass;
    }

    void TearDown() override
    {
        graphics.wait_idle();
        EXPECT_EQ(graphics.debug_error_count(), 0U);
    } ///< Inspect validation after completion.
};

/// @brief Stable submission preserves blending order even when one pass spans several workers.
TEST_F(parallel_recording_test, matches_sequential_pixels_and_reuses_completed_contexts)
{
    const std::array passes{description()};
    const renderer::frame_plan frame{passes};
    resources.wait(renderer.submit(frame));
    const auto reference = resources.readback(target);
    for (unsigned iteration = 0; iteration < 4; ++iteration)
    {
        resources.wait(renderer.submit(frame, adapter, {4, 4, true}));
        EXPECT_EQ(renderer.last_recording().command_lists, 4U);
        EXPECT_EQ(renderer.last_recording().worker_batches, 4U);
        EXPECT_EQ(resources.readback(target), reference);
    }

    std::ostringstream trace;
    profile->write_chrome_trace(trace);
    EXPECT_EQ(trace.str().find("renderer.record.batch") != std::string::npos,
              diagnostics::instrumentation_enabled);
    EXPECT_EQ(trace.str().find("renderer.record.join") != std::string::npos,
              diagnostics::instrumentation_enabled);
    EXPECT_EQ(profile->statistics().errors, 0U);
}

/// @brief Small frames bypass scheduler admission entirely, even if its pool has shut down.
TEST_F(parallel_recording_test, keeps_small_work_on_coordinator)
{
    const std::array passes{description()};
    pool.shutdown();
    EXPECT_NO_THROW(resources.wait(renderer.submit(renderer::frame_plan{passes}, adapter)));
    EXPECT_EQ(renderer.last_recording().worker_batches, 0U);
}

/// @brief Software rasterization keeps its CPU budget unless parallel recording is explicitly
/// requested.
TEST_F(parallel_recording_test, software_backend_defaults_to_sequential_recording)
{
    const std::array passes{description()};
    pool.shutdown();
    EXPECT_NO_THROW(resources.wait(renderer.submit(renderer::frame_plan{passes}, adapter, {1, 4})));
    EXPECT_EQ(renderer.last_recording().worker_batches, 0U);
}

/// @brief Pumped coordinator callbacks cannot invalidate the resolved resource transaction.
TEST_F(parallel_recording_test, rejects_reentrant_store_access_during_recording)
{
    const std::array passes{description()};
    callback_executor callbacks;
    callbacks.callback = [&]
    {
        EXPECT_THROW(resources.state(target), std::logic_error);
        EXPECT_THROW(resources.release(target), std::logic_error);
        EXPECT_THROW(resources.collect(), std::logic_error);
        EXPECT_THROW(renderer.create_pipeline({}), std::logic_error);
    };
    resources.wait(renderer.submit(renderer::frame_plan{passes}, callbacks, {1, 4, true}));
    EXPECT_TRUE(resources.ready(target));
}

/// @brief CPU completion order never changes GPU blending order.
TEST_F(parallel_recording_test, reverse_recording_order_matches_sequential_submission)
{
    const std::array passes{description()};
    const renderer::frame_plan frame{passes};
    resources.wait(renderer.submit(frame));
    const auto reference = resources.readback(target);
    reverse_executor reversed;
    resources.wait(renderer.submit(frame, reversed, {1, 4, true}));
    EXPECT_EQ(resources.readback(target), reference);
}

/// @brief Missing or cancelled batches cannot publish GPU commands, even after earlier CPU
/// recording.
TEST_F(parallel_recording_test, rejects_incomplete_and_cancelled_recording_before_gpu_execution)
{
    const std::array passes{description()};
    const renderer::frame_plan frame{passes};
    const auto native = std::static_pointer_cast<renderer::detail::native_device>(
        renderer::detail::device_access::retain(graphics));
    const auto before = native->next_fence;
    incomplete_executor incomplete;
    EXPECT_THROW(renderer.submit(frame, incomplete, {1, 4, true}), std::logic_error);
    std::stop_source stop;
    stop.request_stop();
    executor cancelled{pool.dispatcher().with_options({.cancellation = stop.get_token()})};
    EXPECT_THROW(renderer.submit(frame, cancelled, {1, 4, true}), jobs::job_error);
    EXPECT_EQ(native->next_fence, before);
    EXPECT_FALSE(resources.ready(target));
    EXPECT_EQ(renderer.last_recording().command_lists, 0U);
    EXPECT_NO_THROW(resources.wait(renderer.submit(frame, adapter, {1, 4, true})));
}

/// @brief Texture dependencies crossing recording batches preserve barriers and pass operations.
TEST_F(parallel_recording_test, samples_prior_pass_output_across_batch_boundaries)
{
    auto first = description();
    auto second = first;
    second.color.target = resources.create_texture({16, 16, renderer::texture_role::color_target});
    second.draws.resize(16);
    renderer::pipeline_description textured;
    textured.program = renderer::shader_program::textured;
    const auto state = renderer.create_pipeline(textured);
    const auto view = resources.create_view(target);
    for (auto& draw : second.draws)
    {
        draw.pipeline = state;
        draw.texture = view;
        draw.tint = {1, 1, 1, 1};
    }

    const std::array passes{first, second};
    const renderer::frame_plan frame{passes};
    resources.wait(renderer.submit(frame));
    const auto reference = resources.readback(second.color.target);
    resources.wait(renderer.submit(frame, adapter, {1, 4, true}));
    EXPECT_EQ(resources.readback(second.color.target), reference);
}

/// @brief A stopped scheduler cannot partially submit or initialize the frame.
TEST_F(parallel_recording_test, failure_before_join_preserves_gpu_and_resource_state)
{
    const std::array passes{description()};
    pool.shutdown();
    const auto native = std::static_pointer_cast<renderer::detail::native_device>(
        renderer::detail::device_access::retain(graphics));
    const auto before = native->next_fence;
    EXPECT_THROW(renderer.submit(renderer::frame_plan{passes}, adapter, {1, 4, true}),
                 std::logic_error);
    EXPECT_EQ(native->next_fence, before);
    EXPECT_FALSE(resources.ready(target));
    EXPECT_EQ(resources.state(target), renderer::resource_state::copy_destination);
    EXPECT_NO_THROW(resources.wait(renderer.submit(renderer::frame_plan{passes})));
}

/// @brief Every context remains retained behind the frame fence until explicit GPU release.
TEST_F(parallel_recording_test, protects_all_recording_contexts_while_frame_is_in_flight)
{
    renderer::pass_renderer bounded{resources, {4, 4, 128, 1, 4}};
    auto pass = description();
    renderer::pipeline_description pipeline;
    pipeline.blend = renderer::blend_mode::straight_alpha;
    const auto state = bounded.create_pipeline(pipeline);
    for (auto& draw : pass.draws)
    {
        draw.pipeline = state;
    }

    const std::array passes{pass};
    renderer::tests::gpu_gate gate{graphics};
    const auto done = bounded.submit(renderer::frame_plan{passes}, adapter, {1, 4, true});
    EXPECT_THROW(bounded.submit(renderer::frame_plan{passes}, adapter, {1, 4, true}),
                 std::overflow_error);
    gate.open();
    resources.wait(done);
    EXPECT_NO_THROW(
        resources.wait(bounded.submit(renderer::frame_plan{passes}, adapter, {1, 4, true})));
}
} // namespace atlas::renderer_jobs::tests
