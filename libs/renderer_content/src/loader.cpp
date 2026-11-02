/** @file @brief Decode ready streamer buffers on its existing jobs and publish owned results. */
#include <algorithm>
#include <atlas/renderer_content/loader.hpp>

namespace atlas::renderer_content
{
namespace
{
/// @brief Acquisition failures and cancellation remain original request failures.
std::shared_ptr<const streaming::asset_bytes> acquired(streaming::read_result result,
                                                       const std::stop_token& stop)
{
    if (stop.stop_requested())
    {
        throw jobs::job_cancelled{};
    }

    if (const auto* error = std::get_if<std::exception_ptr>(&result))
    {
        std::rethrow_exception(*error);
    }

    return std::get<std::shared_ptr<const streaming::asset_bytes>>(std::move(result));
}
} // namespace

/// @brief Copy dependencies into accepted callbacks rather than borrowing the loader facade.
content_loader::content_loader(streaming::asset_streamer& streamer,
                               std::shared_ptr<const image_decoder> decoder,
                               std::shared_ptr<std::pmr::memory_resource> memory,
                               diagnostics::services destinations)
    : streamer_(streamer), decoder_(std::move(decoder)), memory_(std::move(memory)),
      destinations_(std::move(destinations))
{
    if (!decoder_)
    {
        throw std::invalid_argument("content loader needs an image decoder");
    }

    diagnostics::write(destinations_, diagnostics::log_level::info, "renderer.content",
                       "Content loader initialized");
}

/// @brief Streamer ready delivery is already CPU work; never schedule a worker that waits for I/O.
image_request content_loader::read_image(streaming::read_request request, image_limits limits,
                                         jobs::job_priority priority)
{
    const diagnostics::operation_scope scope{destinations_,
                                             {"content.image.request", "content.cpu"}};
    limits.validate();
    auto state = std::make_shared<content_result<std::shared_ptr<const image_data>>>();
    const auto stop = state->cancellation.get_token();
    auto completion =
        streamer_.read(std::move(request),
                       [state, decoder = decoder_, memory = memory_, destinations = destinations_,
                        limits, stop](streaming::read_result result)
                       {
                           const diagnostics::operation_scope decode{
                               destinations, {"content.image.decode", "content.cpu"}};
                           const auto bytes = acquired(std::move(result), stop);
                           auto image = decoder->decode(*bytes, limits, memory, stop);
                           if (!image)
                           {
                               throw std::runtime_error("decoder returned no image");
                           }

                           image->validate(limits);
                           if (stop.stop_requested())
                           {
                               throw jobs::job_cancelled{};
                           }

                           state->value = std::move(image);
                       },
                       {priority, stop});
    return {std::move(state), std::move(completion)};
}

/// @brief Bounded source preparation runs in ready delivery; compilation belongs to the GPU owner.
shader_request content_loader::read_shader(streaming::read_request request,
                                           jobs::job_priority priority)
{
    const diagnostics::operation_scope scope{destinations_,
                                             {"content.shader.request", "content.cpu"}};
    if (request.size == 0)
    {
        throw std::invalid_argument("shader source cannot be empty");
    }

    if (request.size > std::size_t{1024} * 1024)
    {
        throw std::length_error("shader source exceeds 1 MiB");
    }

    auto state = std::make_shared<content_result<std::string>>();
    const auto stop = state->cancellation.get_token();
    auto completion =
        streamer_.read(std::move(request),
                       [state, destinations = destinations_, stop](streaming::read_result result)
                       {
                           const diagnostics::operation_scope prepare{
                               destinations, {"content.shader.prepare", "content.cpu"}};
                           const auto bytes = acquired(std::move(result), stop);
                           if (std::find(bytes->begin(), bytes->end(), 0) != bytes->end())
                           {
                               throw std::invalid_argument("shader source contains embedded NUL");
                           }

                           state->value.assign(bytes->begin(), bytes->end());
                           if (state->value.starts_with("\xEF\xBB\xBF"))
                           {
                               state->value.erase(0, 3);
                           }

                           if (state->value.empty())
                           {
                               throw std::invalid_argument("shader source cannot be empty");
                           }

                           if (stop.stop_requested())
                           {
                               throw jobs::job_cancelled{};
                           }
                       },
                       {priority, stop});
    return {std::move(state), std::move(completion)};
}
} // namespace atlas::renderer_content
