/**
 * @file
 * @brief Behavioral coverage for media pipeline with isolated scenario ownership and cleanup.
 */

#include <atlas/streaming/asset_streamer.hpp>
#include <gtest/gtest.h>
#include <opus/opus.h>

#include <array>
#include <memory>
#include <thread>

namespace atlas::streaming::tests
{
/// @brief Verify decodes streamed opus bytes on a job without opening a file.
TEST(media_pipeline_tests, decodes_streamed_opus_bytes_on_a_job_without_opening_a_file)
{
    jobs::job_system jobs{2};
    // Standard 20 ms Opus silence packet; no encoder or file access is needed.
    auto packet = std::make_shared<const asset_bytes>(asset_bytes{0xF8, 0xFF, 0xFE});
    asset_streamer streamer{jobs, {{"click", memory_asset{packet}}}};
    const auto coordinator = std::this_thread::get_id();
    streamer
        .read({"click", 0, packet->size()},
              [&](const read_result& result)
              {
                  EXPECT_NE(std::this_thread::get_id(), coordinator);
                  ASSERT_TRUE(std::holds_alternative<std::shared_ptr<const asset_bytes>>(result));
                  const auto& bytes = *std::get<std::shared_ptr<const asset_bytes>>(result);
                  int error = OPUS_OK;
                  const std::unique_ptr<OpusDecoder, decltype(&opus_decoder_destroy)> decoder{
                      opus_decoder_create(48000, 2, &error), &opus_decoder_destroy};
                  ASSERT_EQ(error, OPUS_OK);
                  ASSERT_NE(decoder, nullptr);
                  std::array<float, std::size_t{960} * 2> pcm{};
                  EXPECT_EQ(opus_decode_float(decoder.get(), bytes.data(),
                                              static_cast<opus_int32>(bytes.size()), pcm.data(),
                                              960, 0),
                            960);
              })
        .get();
}
} // namespace atlas::streaming::tests
