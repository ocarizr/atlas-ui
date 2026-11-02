/** @file @brief Async content uses ready jobs, propagates failure and owns callback payloads. */
#include <atlas/renderer_content/loader.hpp>
#include <gtest/gtest.h>
#include <sstream>

namespace atlas::renderer_content::tests
{
/// @brief Decode a fixed pixel and record that execution entered a worker, without storage access.
class pixel_decoder final : public image_decoder
{
  public:
    std::shared_ptr<const image_data> decode(std::span<const std::uint8_t> bytes,
                                             image_limits limits,
                                             std::shared_ptr<std::pmr::memory_resource> memory,
                                             std::stop_token stop) const override
    {
        EXPECT_THROW(jobs::require_coordinator_thread(), std::logic_error);
        if (stop.stop_requested())
        {
            throw jobs::job_cancelled{};
        }

        if (bytes.size() != 4)
        {
            throw std::invalid_argument("four pixel bytes required");
        }

        auto image = std::make_shared<image_data>(std::move(memory));
        image->width = image->height = 1;
        for (auto value : bytes)
        {
            image->pixels.push_back(static_cast<std::byte>(value));
        }

        image->validate(limits);
        return image;
    } ///< Simulate reentrant buffer-only CPU decoding.
};

/// @brief Shared application pool and memory sources are isolated per test.
class loader_test : public ::testing::Test
{
  protected:
    jobs::job_system pool{2}; ///< Only application-owned CPU workers.
    /// @brief Immutable in-memory source owned independently of returned decoded pixels.
    std::shared_ptr<const streaming::asset_bytes> bytes =
        std::make_shared<const streaming::asset_bytes>(streaming::asset_bytes{1, 2, 3, 4});
    streaming::asset_streamer streamer{
        pool,
        {{"pixel", streaming::memory_asset{bytes}}}}; ///< Isolated catalog and delivery owner.
    content_loader loader{streamer, std::make_shared<pixel_decoder>()}; ///< Injected strategy.
};

/// @brief A successful decode owns its pixels after callback completion and loader destruction.
TEST_F(loader_test, returns_owned_image_from_ready_job)
{
    auto request = loader.read_image({"pixel", 0, 4});
    const auto image = request.get();
    ASSERT_TRUE(image);
    EXPECT_EQ(image->pixels.size(), 4U);
    EXPECT_EQ(image->pixels[2], std::byte{3});
    EXPECT_EQ(streamer.statistics().resident_bytes, 0U);
}

/// @brief Original acquisition errors reach the request rather than becoming an empty image.
TEST_F(loader_test, propagates_missing_asset)
{
    auto request = loader.read_image({"missing", 0, 4});
    EXPECT_THROW(request.get(), std::exception);
}

/// @brief Output caps are checked by the decoder before publishing successful content.
TEST_F(loader_test, propagates_decode_failure)
{
    auto request = loader.read_image({"pixel", 0, 3});
    EXPECT_THROW(request.get(), std::invalid_argument);
}

/// @brief Cancellation remains observable even if the short decode raced with the stop request.
TEST_F(loader_test, cancellation_never_publishes_a_consumable_image)
{
    auto request = loader.read_image({"pixel", 0, 4});
    request.cancel();
    EXPECT_THROW(request.get(), jobs::job_cancelled);
}

/// @brief Shader admission refuses an empty or oversized source before storage work.
TEST_F(loader_test, rejects_invalid_shader_ranges)
{
    EXPECT_THROW(loader.read_shader({"pixel", 0, 0}), std::invalid_argument);
    EXPECT_THROW(loader.read_shader({"pixel", 0, 1024 * 1024 + 1}), std::length_error);
}

/// @brief Source preparation preserves text, removes BOM and rejects embedded NUL data.
TEST_F(loader_test, prepares_bounded_shader_source_without_direct_storage)
{
    auto text = std::make_shared<const streaming::asset_bytes>(
        streaming::asset_bytes{0xEF, 0xBB, 0xBF, 'x'});
    auto invalid =
        std::make_shared<const streaming::asset_bytes>(streaming::asset_bytes{'x', 0, 'y'});
    streaming::asset_streamer source{
        pool,
        {{"text", streaming::memory_asset{text}}, {"invalid", streaming::memory_asset{invalid}}}};
    content_loader consumer{source, std::make_shared<pixel_decoder>()};
    auto valid = consumer.read_shader({"text", 0, 4});
    EXPECT_EQ(valid.get(), "x");
    auto bad = consumer.read_shader({"invalid", 0, 3});
    EXPECT_THROW(bad.get(), std::invalid_argument);
}

/// @brief Accepted callbacks retain the decoder independently of the short-lived facade.
TEST_F(loader_test, request_outlives_loader_and_retains_allocator)
{
    auto memory = std::make_shared<std::pmr::synchronized_pool_resource>();
    const std::weak_ptr<std::pmr::memory_resource> lifetime{memory};
    auto request = [&]
    {
        content_loader temporary{streamer, std::make_shared<pixel_decoder>(), memory};
        return temporary.read_image({"pixel", 0, 4});
    }();
    memory.reset();
    auto image = request.get();
    EXPECT_FALSE(lifetime.expired());
    EXPECT_EQ(image->pixels.get_allocator().resource(), image->memory.get());
    image.reset();
    streamer.shutdown();
    EXPECT_TRUE(lifetime.expired());
}

/// @brief Successful and failing ready delivery use the same injected CPU capture policy.
TEST_F(loader_test, captures_decode_and_prepare_with_optional_failure_events)
{
    auto capture = std::make_shared<diagnostics::profiler>();
    content_loader observed{streamer, std::make_shared<pixel_decoder>(), {}, {{}, capture}};
    auto success = observed.read_image({"pixel", 0, 4});
    EXPECT_TRUE(success.get());
    auto failure = observed.read_image({"pixel", 0, 3});
    EXPECT_THROW(failure.get(), std::invalid_argument);
    auto shader = observed.read_shader({"pixel", 0, 4});
    EXPECT_EQ(shader.get().size(), 4U);
    std::ostringstream trace;
    capture->write_chrome_trace(trace);
    for (const auto* name :
         {"content.image.request", "content.image.decode", "content.shader.prepare"})
    {
        EXPECT_EQ(trace.str().find(name) != std::string::npos,
                  diagnostics::instrumentation_enabled);
    }

    EXPECT_EQ(trace.str().find("\"result\":\"failed\"") != std::string::npos,
              diagnostics::instrumentation_enabled);
    EXPECT_EQ(capture->statistics().errors, 0U);
}
} // namespace atlas::renderer_content::tests
