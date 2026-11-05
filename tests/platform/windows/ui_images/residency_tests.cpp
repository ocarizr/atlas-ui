/** @file @brief Resident placeholders, actual texture publication and logical GPU quotas. */
#include "../../../ui_images/image_test_support.hpp"
#include <atlas/ui/element_tree.hpp>
#include <atlas/ui_images/image_renderer.hpp>

namespace atlas::ui_images::tests
{
class residency_test : public cache_test
{
  protected:
    frame_payload frame(std::uint64_t key)
    {
        ui::element_tree tree;
        const auto node = tree.create();
        ui::paint_style style;
        style.fill = true;
        style.texture = key;
        tree.set_paint(node, style);
        tree.set_viewport({0, 0, 16, 16});
        const auto source = tree.snapshot();
        ui::layout_engine layout{workers.dispatcher()};
        ui::paint_engine paint{workers.dispatcher()};
        return {paint.calculate(source, layout.calculate(source)), cache.snapshot()};
    } ///< Produce immutable input without retaining a mutable tree.

    void TearDown() override
    {
        graphics.wait_idle();
        EXPECT_EQ(graphics.debug_error_count(), 0);
    } ///< Validate native operations after every success or failure.

    renderer::device graphics{{renderer::adapter_preference::software}}; ///< WARP pixel reference.
    renderer::resource_store resources{graphics}; ///< Fence-retained native storage.
    renderer::pass_renderer passes{resources};    ///< Existing generic passes.
    renderer::texture_handle target =
        resources.create_texture({16, 16, renderer::texture_role::color_target}); ///< Output.
}; ///< Shared CPU fixture and one isolated GPU domain per fact.

/// @brief Pending/unknown keys draw an initialized fallback without blocking asset acquisition.
TEST_F(residency_test, MissingImageUsesGrayPlaceholder)
{
    image_renderer images{resources, passes, {1, 4, 1}};
    graphics.wait_idle();
    ASSERT_TRUE(images.ready());
    resources.wait(images.submit(frame(999), target, {16, 16}));
    const auto pixels = resources.readback(target);
    EXPECT_EQ(std::to_integer<unsigned>(pixels[0]), 128);
    EXPECT_EQ(std::to_integer<unsigned>(pixels[2]), 128);
    EXPECT_EQ(images.statistics().resident_bytes, 4);
}

/// @brief GPU readiness, rather than CPU decode completion, controls publication of a real view.
TEST_F(residency_test, PublishesBlueImageAfterUploadAndRespectsBudget)
{
    const auto key = cache.request({"pixel", 0, 4}, {16, 4});
    settle(key);
    const auto packet = frame(key);
    image_renderer images{resources, passes, {1, 8, 1}};
    graphics.wait_idle();
    resources.wait(images.submit(packet, target, {16, 16}));
    graphics.wait_idle();
    resources.wait(images.submit(packet, target, {16, 16}));
    const auto pixels = resources.readback(target);
    EXPECT_EQ(std::to_integer<unsigned>(pixels[0]), 0);
    EXPECT_EQ(std::to_integer<unsigned>(pixels[2]), 255);
    EXPECT_EQ(images.statistics().resident_bytes, 8);
    EXPECT_LE(images.statistics().peak_resident_bytes, 8);
}

/// @brief An insufficient logical resident budget defers uploads while keeping the UI visible.
TEST_F(residency_test, QuotaDefersImageWithoutExceedingResidentBudget)
{
    const auto key = cache.request({"pixel", 0, 4}, {16, 4});
    settle(key);
    image_renderer images{resources, passes, {1, 4, 1}};
    graphics.wait_idle();
    resources.wait(images.submit(frame(key), target, {16, 16}));
    EXPECT_EQ(images.statistics().deferred, 1);
    EXPECT_EQ(images.statistics().resident_bytes, 4);
    EXPECT_EQ(std::to_integer<unsigned>(resources.readback(target)[0]), 128);
}

/// @brief A cancelled frame replaces previously resident content and releases its logical charge.
TEST_F(residency_test, CancelledImageRetiresAfterLastUseAndReturnsToPlaceholder)
{
    const auto key = cache.request({"pixel", 0, 4}, {16, 4});
    settle(key);
    const auto old = frame(key);
    image_renderer images{resources, passes, {1, 8, 1}};
    graphics.wait_idle();
    resources.wait(images.submit(old, target, {16, 16}));
    graphics.wait_idle();
    resources.wait(images.submit(old, target, {16, 16}));
    EXPECT_EQ(images.statistics().drawn_images, 1);
    cache.cancel(key);
    resources.wait(images.submit(frame(key), target, {16, 16}));
    images.collect();
    EXPECT_EQ(images.statistics().resident_bytes, 4);
    EXPECT_EQ(images.statistics().drawn_images, 0);
    EXPECT_EQ(std::to_integer<unsigned>(resources.readback(target)[0]), 128);
    EXPECT_TRUE(old.images.find(key)->pixels);
}

/// @brief Failed acquisition remains visible as a placeholder without creating an image upload.
TEST_F(residency_test, FailedAcquisitionDoesNotAllocateResidentImage)
{
    const auto key = cache.request({"missing", 0, 4}, {16, 4});
    settle(key);
    image_renderer images{resources, passes};
    graphics.wait_idle();
    resources.wait(images.submit(frame(key), target, {16, 16}));
    EXPECT_EQ(images.statistics().images, 0);
    EXPECT_EQ(images.statistics().resident_bytes, 4);
    EXPECT_EQ(std::to_integer<unsigned>(resources.readback(target)[0]), 128);
}
} // namespace atlas::ui_images::tests
