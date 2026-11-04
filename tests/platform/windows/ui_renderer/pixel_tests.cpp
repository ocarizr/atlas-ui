/** @file @brief Software D3D12 validates ordered UI pixels, clips, textures and retirement. */
#include "../../../ui/resource_test_support.hpp"
#include <atlas/jobs/job_system.hpp>
#include <atlas/ui/element_tree.hpp>
#include <atlas/ui_renderer/paint_renderer.hpp>
#include <gtest/gtest.h>

namespace atlas::ui_renderer::tests
{
class pixel_test : public ::testing::Test
{
  protected:
    void TearDown() override
    {
        graphics.wait_idle();
        EXPECT_EQ(graphics.debug_error_count(), 0u);
    } ///< Drain every frame before checking native validation.

    renderer::device graphics{{renderer::adapter_preference::software}}; ///< WARP reference.
    renderer::resource_store resources{graphics};   ///< Fence-retained resources.
    renderer::pass_renderer passes{resources};      ///< Generic ordered pass submission.
    paint_renderer renderer{resources, passes};     ///< Platform-neutral UI adapter.
    jobs::job_system workers{2};                    ///< Shared CPU pool.
    ui::element_tree tree;                          ///< Owner-local test content.
    ui::layout_engine layout{workers.dispatcher()}; ///< Geometry source.
    ui::paint_engine paint{workers.dispatcher()};   ///< Immutable paint packets.
}; ///< RAII releases services after their users.

/// @brief Verify foreground and clip produce reference pixels and retire geometry.
TEST_F(pixel_test, ForegroundAndClipProduceReferencePixelsAndRetireGeometry)
{
    const auto background = tree.create();
    ui::paint_style red;
    red.fill = true;
    red.color = {1, 0, 0, 1};
    red.layer = 10;
    tree.set_paint(background, red);
    const auto front = tree.create();
    ui::layout_style size;
    size.intrinsic = {8, 8};
    size.horizontal = ui::layout_alignment::start;
    size.vertical = ui::layout_alignment::start;
    tree.set_layout(front, size);
    auto green = red;
    green.layer = 0;
    green.color = {0, 1, 0, 1};
    green.clip = true;
    tree.set_paint(front, green);
    tree.set_viewport({0, 0, 16, 16});
    const auto input = tree.snapshot();
    const auto packet = paint.calculate(input, layout.calculate(input));
    const auto target = resources.create_texture({16, 16, renderer::texture_role::color_target});
    const auto done = renderer.submit(packet, target, {16, 16});
    resources.wait(done);
    const auto pixels = resources.readback(target);
    EXPECT_EQ(std::to_integer<unsigned>(pixels[0]), 0u);
    EXPECT_EQ(std::to_integer<unsigned>(pixels[1]), 255u);
    const auto far = (12u * 16u + 12u) * 4u;
    EXPECT_EQ(std::to_integer<unsigned>(pixels[far]), 255u);
    EXPECT_EQ(std::to_integer<unsigned>(pixels[far + 1]), 0u);
    resources.collect();
    EXPECT_EQ(resources.statistics().retired_resources, 0u);
}

/// @brief Verify resident texture clips oversized child and survives handle retirement.
TEST_F(pixel_test, ResidentTextureClipsOversizedChildAndSurvivesHandleRetirement)
{
    const auto root = tree.create();
    ui::layout_style fixed;
    fixed.constraints.minimum = {8, 8};
    fixed.constraints.maximum = {8, 8};
    fixed.horizontal = ui::layout_alignment::start;
    fixed.vertical = ui::layout_alignment::start;
    tree.set_layout(root, fixed);
    ui::paint_style clip;
    clip.clip = true;
    tree.set_paint(root, clip);
    const auto child = tree.create(root);
    fixed.constraints.minimum = {16, 16};
    fixed.constraints.maximum = {16, 16};
    tree.set_layout(child, fixed);
    clip.fill = true;
    clip.clip = false;
    clip.texture = 7;
    tree.set_paint(child, clip);
    tree.set_viewport({0, 0, 16, 16});
    const auto input = tree.snapshot();
    const auto packet = paint.calculate(input, layout.calculate(input));
    const auto image = resources.create_texture({1, 1});
    const std::array<std::byte, 4> blue{std::byte{0}, std::byte{0}, std::byte{255}, std::byte{255}};
    resources.upload(image, blue);
    const auto view = resources.create_view(image);
    const auto target = resources.create_texture({16, 16, renderer::texture_role::color_target});
    const std::array bindings{texture_binding{7, view}};
    const auto done = renderer.submit(packet, target, {16, 16}, bindings);
    resources.release(view);
    resources.release(image);
    resources.wait(done);
    const auto pixels = resources.readback(target);
    EXPECT_EQ(std::to_integer<unsigned>(pixels[2]), 255u);
    const auto outside = (12u * 16u + 12u) * 4u;
    EXPECT_NE(std::to_integer<unsigned>(pixels[outside + 2]), 255u);
    resources.collect();
    EXPECT_EQ(resources.statistics().retired_descriptors, 0u);
    EXPECT_EQ(resources.statistics().retired_resources, 0u);
}

/// @brief Verify missing texture and duplicate keys reject before gpu allocation or target write.
TEST_F(pixel_test, MissingTextureAndDuplicateKeysRejectBeforeGpuAllocationOrTargetWrite)
{
    const auto node = tree.create();
    ui::paint_style style;
    style.fill = true;
    style.texture = 7;
    tree.set_paint(node, style);
    tree.set_viewport({0, 0, 16, 16});
    const auto source = tree.snapshot();
    const auto packet = paint.calculate(source, layout.calculate(source));
    const auto target = resources.create_texture({16, 16, renderer::texture_role::color_target});
    renderer::pass_description initial;
    initial.color.target = target;
    initial.color.clear_color = {0.2f, 0.4f, 0.6f, 1};
    const std::array initial_passes{initial};
    resources.wait(passes.submit(renderer::frame_plan{initial_passes}));
    const auto before_pixels = resources.readback(target);
    const auto before = resources.statistics();
    EXPECT_THROW(renderer.submit(packet, target, {16, 16}), std::invalid_argument);
    EXPECT_EQ(resources.statistics().live_resources, before.live_resources);
    EXPECT_EQ(resources.statistics().pending_transfers, before.pending_transfers);
    const auto view = resources.create_view(target);
    const std::array duplicate{texture_binding{7, view}, texture_binding{7, view}};
    EXPECT_THROW(renderer.submit(packet, target, {16, 16}, duplicate), std::invalid_argument);
    EXPECT_EQ(renderer.last_draws(), 0);
    EXPECT_EQ(resources.readback(target), before_pixels);
    resources.release(view);
}

/// @brief Verify adjacent compatible quads batch without reordering alpha.
TEST_F(pixel_test, AdjacentCompatibleQuadsBatchWithoutReorderingAlpha)
{
    const auto back = tree.create();
    const auto front = tree.create();
    ui::paint_style style;
    style.fill = true;
    style.color = {1, 0, 0, 1};
    tree.set_paint(back, style);
    style.color = {0, 1, 0, 0.5f};
    tree.set_paint(front, style);
    tree.set_viewport({0, 0, 16, 16});
    const auto source = tree.snapshot();
    const auto packet = paint.calculate(source, layout.calculate(source));
    const auto target = resources.create_texture({16, 16, renderer::texture_role::color_target});
    resources.wait(renderer.submit(packet, target, {16, 16}));
    EXPECT_EQ(renderer.last_draws(), 1);
    const auto pixels = resources.readback(target);
    EXPECT_NEAR(std::to_integer<unsigned>(pixels[0]), 128, 1);
    EXPECT_NEAR(std::to_integer<unsigned>(pixels[1]), 128, 1);
    EXPECT_EQ(std::to_integer<unsigned>(pixels[2]), 0);
}

/// @brief Logical viewport origins and pixel scaling preserve a fixed nested mask.
TEST_F(pixel_test, OffsetLogicalViewportScalesClipToPhysicalAttachment)
{
    const auto root = tree.create();
    ui::layout_style size;
    size.constraints.minimum = {8, 8};
    size.constraints.maximum = {8, 8};
    size.horizontal = ui::layout_alignment::start;
    size.vertical = ui::layout_alignment::start;
    tree.set_layout(root, size);
    ui::paint_style style;
    style.clip = true;
    tree.set_paint(root, style);
    const auto child = tree.create(root);
    style.fill = true;
    style.clip = false;
    style.color = {1, 1, 1, 1};
    tree.set_paint(child, style);
    size.constraints.minimum = {16, 16};
    size.constraints.maximum = {16, 16};
    tree.set_layout(child, size);
    tree.set_viewport({100, 50, 16, 16});
    const auto source = tree.snapshot();
    const auto packet = paint.calculate(source, layout.calculate(source));
    const auto target = resources.create_texture({32, 32, renderer::texture_role::color_target});
    resources.wait(renderer.submit(packet, target, {32, 32}));
    const auto pixels = resources.readback(target);
    const auto inside = (15u * 32u + 15u) * 4u;
    const auto outside = (16u * 32u + 16u) * 4u;
    EXPECT_EQ(std::to_integer<unsigned>(pixels[inside]), 255);
    EXPECT_NE(std::to_integer<unsigned>(pixels[outside]), 255);
}

/// @brief Every injected PMR failure unwinds CPU preparation without allocating GPU geometry.
TEST_F(pixel_test, PmrFailurePointsReleaseScratchAndPermitRetry)
{
    const auto node = tree.create();
    ui::paint_style style;
    style.fill = true;
    tree.set_paint(node, style);
    tree.set_viewport({0, 0, 16, 16});
    const auto source = tree.snapshot();
    const auto packet = paint.calculate(source, layout.calculate(source));
    const auto target = resources.create_texture({16, 16, renderer::texture_role::color_target});
    auto memory = std::make_shared<ui::tests::failing_resource>();
    paint_renderer checked{resources, passes, memory};
    const auto calls = memory->calls;
    resources.wait(checked.submit(packet, target, {16, 16}));
    const auto count = memory->calls - calls;
    resources.collect();
    const auto baseline = resources.statistics().live_resources;
    for (std::size_t i = 0; i < count; ++i)
    {
        memory->remaining = i;
        EXPECT_THROW(checked.submit(packet, target, {16, 16}), std::bad_alloc);
        memory->remaining = std::numeric_limits<std::size_t>::max();
        EXPECT_EQ(memory->live_bytes, 0);
        EXPECT_EQ(resources.statistics().live_resources, baseline);
    }

    resources.wait(checked.submit(packet, target, {16, 16}));
}

/// @brief Failed pass validation retires an uploaded temporary buffer without leaking its slot.
TEST_F(pixel_test, InvalidTargetReleasesUploadedGeometryAfterItsFence)
{
    const auto node = tree.create();
    ui::paint_style style;
    style.fill = true;
    tree.set_paint(node, style);
    tree.set_viewport({0, 0, 16, 16});
    const auto source = tree.snapshot();
    const auto packet = paint.calculate(source, layout.calculate(source));
    const auto before = resources.statistics().live_resources;
    EXPECT_THROW(renderer.submit(packet, {}, {16, 16}), std::invalid_argument);
    graphics.wait_idle();
    resources.collect();
    EXPECT_EQ(resources.statistics().live_resources, before);
    EXPECT_EQ(resources.statistics().retired_resources, 0);
    EXPECT_EQ(resources.statistics().pending_transfers, 0);
    EXPECT_EQ(renderer.last_draws(), 0);
}

/// @brief Constructor rollback frees the first pipeline when the second cannot be admitted.
TEST_F(pixel_test, PipelineAdmissionFailureRollsBackTheFirstPipeline)
{
    renderer::pass_renderer limited{resources, {1}};
    EXPECT_THROW((paint_renderer{resources, limited}), std::overflow_error);
    limited.collect();
    const auto replacement = limited.create_pipeline({});
    EXPECT_NE(replacement.owner, 0);
    limited.release(replacement);
}
} // namespace atlas::ui_renderer::tests
