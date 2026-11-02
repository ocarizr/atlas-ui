/** @file @brief Real loose-image decoding, GPU retention and exact pipeline cache ownership. */
#include "resource_test_support.hpp"
#include <atlas/renderer_content/gpu_content.hpp>
#include <atlas/renderer_content/loader.hpp>
#include <future>
#include <sstream>

namespace atlas::renderer_content::tests
{
/// @brief Isolated WARP rendering plus application-owned streaming workers.
class native_content_test : public ::testing::Test
{
  protected:
    jobs::job_system pool{2}; ///< Persistent application pool.
    /// @brief Actual loose-file acquisition crosses the native streamer adapter.
    streaming::asset_streamer streamer{
        pool,
        {{"image",
          streaming::loose_file_asset{std::filesystem::path{ATLAS_CONTENT_ASSETS} / "checker.bmp"}},
         {"shader",
          streaming::loose_file_asset{std::filesystem::path{ATLAS_CONTENT_ASSETS} / "quad.hlsl"}}}};
    content_loader loader{streamer, make_image_decoder()}; ///< WIC sees only streamer bytes.
    renderer::device graphics{{renderer::adapter_preference::software}}; ///< Pixel reference.
    renderer::resource_store resources{graphics}; ///< Independent storage and GPU boundaries.
    renderer::pass_renderer passes{resources};    ///< Generic draw submission.

    void TearDown() override
    {
        graphics.wait_idle();
        resources.collect();
        EXPECT_EQ(graphics.debug_error_count(), 0U);
    } ///< Drain before checking native validation.

    std::shared_ptr<const image_data> image()
    {
        auto request = loader.read_image({"image", 0, 70});
        return request.get();
    } ///< Decode the checked-in two-by-two BMP through actual loose-file acquisition.
};

/// @brief Pixel order, stride and RGB channel conversion are normalized by the selected adapter.
TEST_F(native_content_test, decodes_loose_image_into_tight_rgba)
{
    const auto decoded = image();
    EXPECT_EQ(decoded->width, 2U);
    EXPECT_EQ(decoded->height, 2U);
    const std::vector<std::byte> expected{
        std::byte{255}, std::byte{0},   std::byte{0},   std::byte{255},
        std::byte{0},   std::byte{255}, std::byte{0},   std::byte{255},
        std::byte{0},   std::byte{0},   std::byte{255}, std::byte{255},
        std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255}};
    EXPECT_EQ(std::vector(decoded->pixels.begin(), decoded->pixels.end()), expected);
}

/// @brief Pending uploads retain decoded data; polling actual fence completion releases it.
TEST_F(native_content_test, retains_pixels_until_upload_fence_and_retires_views)
{
    renderer::tests::gpu_gate gate{graphics};
    auto decoded = image();
    const std::weak_ptr<const image_data> lifetime{decoded};
    texture_upload upload{resources, decoded};
    decoded.reset();
    EXPECT_FALSE(upload.ready());
    EXPECT_FALSE(lifetime.expired());
    EXPECT_THROW(upload.binding(), std::logic_error);
    gate.open();
    graphics.wait_idle();
    EXPECT_TRUE(upload.ready());
    EXPECT_TRUE(lifetime.expired());
    const auto binding = upload.binding();
    EXPECT_EQ(resources.readback(binding.texture).size(), 16U);
    upload.cancel();
    EXPECT_FALSE(upload.ready());
    EXPECT_THROW(upload.binding(), std::logic_error);
    resources.collect();
    EXPECT_EQ(resources.statistics().live_resources, 0U);
    EXPECT_EQ(resources.statistics().live_descriptors, 0U);
}

/// @brief Cancellation invalidates immediately without freeing in-flight native storage.
TEST_F(native_content_test, cancel_pending_upload_defers_native_reclamation)
{
    renderer::tests::gpu_gate gate{graphics};
    texture_upload upload{resources, image()};
    upload.cancel();
    resources.collect();
    EXPECT_EQ(resources.statistics().retired_resources, 1U);
    EXPECT_GT(resources.statistics().staging_bytes, 0U);
    gate.open();
    graphics.wait_idle();
    resources.collect();
    EXPECT_EQ(resources.statistics().allocation_bytes, 0U);
    EXPECT_EQ(resources.statistics().staging_bytes, 0U);
}

/// @brief Exact descriptions reuse one PSO; capacity and clear remain explicit ownership decisions.
TEST_F(native_content_test, cache_reuses_exact_inputs_and_retires_cleared_handles)
{
    pipeline_cache cache{passes, 1};
    const auto first = cache.get({});
    EXPECT_EQ(first, cache.get({}));
    EXPECT_EQ(cache.size(), 1U);
    renderer::pipeline_description different;
    different.blend = renderer::blend_mode::straight_alpha;
    EXPECT_THROW(cache.get(different), std::overflow_error);
    EXPECT_THROW(passes.release({first.owner, first.index, first.generation + 1}),
                 std::invalid_argument);
    cache.clear();
    EXPECT_EQ(cache.size(), 0U);
    EXPECT_THROW(passes.release(first), std::invalid_argument);
}

/// @brief Failed compilation publishes no entry and does not exhaust cache capacity.
TEST_F(native_content_test, cache_does_not_remember_compile_failure)
{
    pipeline_cache cache{passes, 1};
    renderer::pipeline_description invalid;
    invalid.program = renderer::shader_program::custom;
    invalid.source = "invalid shader";
    EXPECT_THROW(cache.get(invalid), std::exception);
    EXPECT_EQ(cache.size(), 0U);
    EXPECT_NO_THROW(cache.get({}));
}

/// @brief Cached hits must reject foreign threads just like native pipeline creation does.
TEST_F(native_content_test, cache_hits_enforce_coordinator_ownership)
{
    pipeline_cache cache{passes};
    (void)cache.get({});
    auto result = std::async(std::launch::async,
                             [&]
                             {
                                 EXPECT_THROW(cache.get({}), std::logic_error);
                             });
    result.get();
}

/// @brief The complete loose-image/shader path renders exact sampled pixels through generic draws.
TEST_F(native_content_test, streamed_content_renders_a_textured_primitive)
{
    const auto decoded = image();
    texture_upload upload{resources, decoded};
    graphics.wait_idle();
    const auto binding = upload.binding();
    auto source = loader.read_shader({"shader", 0, ATLAS_CONTENT_SHADER_SIZE});
    renderer::pipeline_description description;
    description.program = renderer::shader_program::custom;
    description.source = source.get();
    description.linear_sampling = false;
    pipeline_cache cache{passes};
    const std::array<renderer::vertex, 3> vertices{{{{-1, -1, 0}, {1, 1, 1, 1}, {0, 1}},
                                                    {{-1, 3, 0}, {1, 1, 1, 1}, {0, -1}},
                                                    {{3, -1, 0}, {1, 1, 1, 1}, {2, 1}}}};
    const auto geometry = resources.create_buffer({sizeof(vertices)});
    resources.upload(geometry, std::as_bytes(std::span{vertices}));
    renderer::draw_packet draw;
    draw.pipeline = cache.get(description);
    draw.vertices = geometry;
    draw.texture = binding.view;
    draw.vertex_count = 3;
    renderer::pass_description pass;
    pass.color.target = resources.create_texture({2, 2, renderer::texture_role::color_target});
    pass.draws.push_back(draw);
    const std::array plan{pass};
    resources.wait(passes.submit(renderer::frame_plan{plan}));
    EXPECT_EQ(resources.readback(pass.color.target),
              std::vector(decoded->pixels.begin(), decoded->pixels.end()));
}

/// @brief WIC rejects corrupt buffers and checks cancellation before native conversion.
TEST_F(native_content_test, rejects_corruption_and_output_limits)
{
    const std::array<std::uint8_t, 4> invalid{1, 2, 3, 4};
    const auto decoder = make_image_decoder();
    EXPECT_THROW(decoder->decode(invalid, {}, {}, {}), std::runtime_error);
    auto limited = loader.read_image({"image", 0, 70}, {1, 4});
    EXPECT_THROW(limited.get(), std::length_error);
    std::stop_source stop;
    stop.request_stop();
    EXPECT_THROW(decoder->decode(invalid, {}, {}, stop.get_token()), jobs::job_cancelled);
}

/// @brief Descriptor failure after upload admission rolls back handles but preserves GPU staging.
TEST_F(native_content_test, failed_view_admission_retires_partial_upload)
{
    renderer::resource_limits limits;
    limits.max_descriptors = 1;
    renderer::resource_store bounded{graphics, limits};
    const auto occupied = bounded.create_texture({1, 1});
    const auto view = bounded.create_view(occupied);
    EXPECT_THROW((texture_upload{bounded, image()}), std::overflow_error);
    graphics.wait_idle();
    bounded.collect();
    EXPECT_EQ(bounded.statistics().live_resources, 1U);
    EXPECT_EQ(bounded.statistics().retired_resources, 0U);
    EXPECT_EQ(bounded.statistics().staging_bytes, 0U);
    bounded.release(view);
    bounded.release(occupied);
}

/// @brief GPU orchestration emits CPU scopes only under the configured capture policy.
TEST_F(native_content_test, captures_upload_and_pipeline_cache_operations)
{
    auto capture = std::make_shared<diagnostics::profiler>();
    {
        pipeline_cache cache{passes, 1, {{}, capture}};
        (void)cache.get({});
        texture_upload upload{resources, image(), {{}, capture}};
        graphics.wait_idle();
        EXPECT_TRUE(upload.ready());
    }
    std::ostringstream trace;
    capture->write_chrome_trace(trace);
    for (const auto* name : {"content.texture.upload", "content.texture.retire",
                             "content.pipeline.get", "content.pipeline.clear"})
    {
        EXPECT_EQ(trace.str().find(name) != std::string::npos,
                  diagnostics::instrumentation_enabled);
    }
}
} // namespace atlas::renderer_content::tests
