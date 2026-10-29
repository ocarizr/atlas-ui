#include <atlas/streaming/AssetStreamer.hpp>
#include <gtest/gtest.h>
#include <opus/opus.h>

#include <array>
#include <memory>
#include <thread>

namespace atlas::streaming::tests
{
TEST(MediaPipelineTests, DecodesStreamedOpusBytesOnAJobWithoutOpeningAFile)
{
    jobs::JobSystem jobs{2};
    // Standard 20 ms Opus silence packet; no encoder or file access is needed.
    auto packet = std::make_shared<const AssetBytes>(AssetBytes{0xF8, 0xFF, 0xFE});
    AssetStreamer streamer{jobs, {{"click", MemoryAsset{packet}}}};
    const auto coordinator = std::this_thread::get_id();
    streamer
        .read({"click", 0, packet->size()},
              [&](const ReadResult& result)
              {
                  EXPECT_NE(std::this_thread::get_id(), coordinator);
                  ASSERT_TRUE(std::holds_alternative<std::shared_ptr<const AssetBytes>>(result));
                  const auto& bytes = *std::get<std::shared_ptr<const AssetBytes>>(result);
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
