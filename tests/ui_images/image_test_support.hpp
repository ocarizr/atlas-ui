/** @file @brief Isolated buffer-only image fixtures with bounded completion deadlines. */
#pragma once
#include <atlas/ui_images/image_cache.hpp>
#include <gtest/gtest.h>
#include <thread>

namespace atlas::ui_images::tests
{
class pixel_decoder final : public renderer_content::image_decoder
{
  public:
    std::shared_ptr<const renderer_content::image_data>
    decode(std::span<const std::uint8_t> bytes, renderer_content::image_limits limits,
           std::shared_ptr<std::pmr::memory_resource> memory, std::stop_token stop) const override
    {
        EXPECT_THROW(jobs::require_coordinator_thread(), std::logic_error);
        if (stop.stop_requested())
        {
            throw jobs::job_cancelled{};
        }

        if (bytes.size() != 4)
        {
            throw std::invalid_argument("expected four pixel bytes");
        }

        auto image = std::make_shared<renderer_content::image_data>(std::move(memory));
        image->width = image->height = 1;
        image->pixels.resize(4);
        for (std::size_t i = 0; i < 4; ++i)
        {
            image->pixels[i] = static_cast<std::byte>(bytes[i]);
        }

        image->validate(limits);
        return image;
    } ///< A reentrant fake codec consumes only the streamer's owned buffer.
}; ///< No direct file I/O, blocking or mutable shared data.

class cache_test : public ::testing::Test
{
  protected:
    void settle(std::uint64_t key)
    {
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds{5};
        while (std::chrono::steady_clock::now() < end)
        {
            cache.poll();
            if (cache.snapshot().find(key)->status != image_status::pending)
            {
                workers.wait_idle();
                return;
            }

            std::this_thread::yield();
        }

        FAIL() << "UI image completion deadline exceeded";
    } ///< Poll without arbitrary sleeps; completion has a diagnostic timeout.

    jobs::job_system workers{2}; ///< Drained after all borrowers.
    std::shared_ptr<const streaming::asset_bytes> bytes =
        std::make_shared<const streaming::asset_bytes>(
            streaming::asset_bytes{0, 0, 255, 255}); ///< Immutable source.
    streaming::asset_streamer streamer{
        workers, {{"pixel", streaming::memory_asset{bytes}}}}; ///< Sole storage boundary.
    image_cache cache{
        streamer, std::make_shared<pixel_decoder>(), {4, 8}}; ///< Tight decoded quota.
}; ///< Per-fact isolated services and automatic worker/streamer teardown.

} // namespace atlas::ui_images::tests
