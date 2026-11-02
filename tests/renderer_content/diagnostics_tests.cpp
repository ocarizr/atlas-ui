/** @file @brief Ready-job failures log through injected destinations without implicit storage. */
#include <atlas/renderer_content/loader.hpp>
#include <gtest/gtest.h>

namespace atlas::renderer_content::tests
{
/// @brief In-memory output observed only after the callback future and logger flush complete.
class memory_output final : public diagnostics::log_output
{
  public:
    void append(std::string_view text) override
    {
        content.append(text);
    } ///< Append serialized logs.

    void flush() override
    {
    } ///< No external backend to flush.

    std::string content; ///< Isolated captured messages.
};

/// @brief Deterministic failure distinguishes codec failure logging from acquisition failure.
class failing_decoder final : public image_decoder
{
  public:
    std::shared_ptr<const image_data> decode(std::span<const std::uint8_t>, image_limits,
                                             std::shared_ptr<std::pmr::memory_resource>,
                                             std::stop_token) const override
    {
        throw std::invalid_argument("test codec failure");
    } ///< Never publishes partially decoded pixels.
};

/// @brief Failure scopes log even when profiling probes are compiled out.
TEST(content_diagnostics, logs_decode_failure_with_injected_output)
{
    jobs::job_system pool{1};
    auto bytes = std::make_shared<const streaming::asset_bytes>(streaming::asset_bytes{1});
    streaming::asset_streamer streamer{pool, {{"image", streaming::memory_asset{bytes}}}};
    auto output = std::make_shared<memory_output>();
    auto log = std::make_shared<diagnostics::logger>(diagnostics::logging_options{}, output);
    content_loader loader{streamer, std::make_shared<failing_decoder>(), {}, {log, {}}};
    auto request = loader.read_image({"image", 0, 1});
    EXPECT_THROW(request.get(), std::invalid_argument);
    log->flush();
    EXPECT_NE(output->content.find("content.image.decode"), std::string::npos);
    EXPECT_NE(output->content.find("Content loader initialized"), std::string::npos);
}
} // namespace atlas::renderer_content::tests
