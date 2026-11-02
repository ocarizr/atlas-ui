/**
 * @file
 * @brief Streamer-fed ready-job decoding without worker waits or direct storage access.
 */
#pragma once
#include <atlas/jobs/execution.hpp>
#include <atlas/renderer_content/images.hpp>

namespace atlas::renderer_content
{
/// @brief Callback-owned result survives loader/request destruction while accepted work drains.
template <class T> struct content_result
{
    std::stop_source cancellation; ///< Shared cooperative cancellation signal.
    T value{}; ///< Published before the streamer's completion future becomes ready.
};

/// @brief Move-only asynchronous request; destroying it neither waits nor cancels accepted work.
template <class T> class content_request
{
  public:
    content_request(std::shared_ptr<content_result<T>> state, std::future<void> completion)
        : state_(std::move(state)), completion_(std::move(completion))
    {
    } ///< Retain result and callback-completion lifetime.

    bool valid() const noexcept
    {
        return completion_.valid();
    } ///< Whether completion can still be consumed.

    bool ready() const
    {
        return valid() &&
               completion_.wait_for(std::chrono::seconds{0}) == std::future_status::ready;
    } ///< Nonblocking observation; a ready request may still have failed.

    void cancel() noexcept
    {
        if (state_)
        {
            state_->cancellation.request_stop();
        }
    } ///< Request storage cancellation and cooperative decode cancellation.

    T get()
    {
        jobs::require_coordinator_thread();
        completion_.get();
        if (state_->cancellation.stop_requested())
        {
            throw jobs::job_cancelled{};
        }

        return std::move(state_->value);
    } ///< Join and consume once on a coordinator; preserves the original failure.
  private:
    std::shared_ptr<content_result<T>> state_; ///< Worker-written until completion publication.
    std::future<void> completion_; ///< Observes callback and dispatch errors as well as success.
};

using image_request = content_request<std::shared_ptr<const image_data>>; ///< Decoded image result.
using shader_request = content_request<std::string>; ///< Bounded in-memory HLSL source result.

/// @brief Stateless consumer orchestration; streamer and its application pool outlive accepted
/// work.
class content_loader
{
  public:
    content_loader(streaming::asset_streamer& streamer,
                   std::shared_ptr<const image_decoder> decoder,
                   std::shared_ptr<std::pmr::memory_resource> memory = {},
                   diagnostics::services destinations =
                       {}); ///< Borrow storage and retain reentrant decode/allocator services.
    image_request read_image(
        streaming::read_request request, image_limits limits = {},
        jobs::job_priority priority =
            jobs::job_priority::background); ///< Admit one bounded image decode on ready delivery.
    shader_request
    read_shader(streaming::read_request request,
                jobs::job_priority priority =
                    jobs::job_priority::background); ///< Read at most 1 MiB of trusted HLSL without
                                                     ///< resolving file includes.
  private:
    streaming::asset_streamer& streamer_; ///< Exclusive storage boundary, borrowed for admission.
    std::shared_ptr<const image_decoder> decoder_;      ///< Reentrant buffer-only decode strategy.
    std::shared_ptr<std::pmr::memory_resource> memory_; ///< Thread-safe shared output allocator.
    diagnostics::services destinations_; ///< Optional retained logging/capture endpoints.
};
} // namespace atlas::renderer_content
