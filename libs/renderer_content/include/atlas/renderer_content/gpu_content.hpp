/**
 * @file
 * @brief Coordinator-owned texture uploads and device-local exact-input pipeline caching.
 */
#pragma once
#include <atlas/renderer/passes.hpp>
#include <atlas/renderer_content/images.hpp>

namespace atlas::renderer_content
{
/// @brief Uploaded sampled texture and its retained descriptor; owned by texture_upload.
struct texture_binding
{
    renderer::texture_handle texture; ///< Valid until its upload owner is destroyed/cancelled.
    renderer::descriptor_handle view; ///< Shader-readable RGBA8 view.
};

/// @brief Coordinator-only ownership; resource_store must outlive this object.
class texture_upload
{
  public:
    texture_upload(renderer::resource_store& resources, std::shared_ptr<const image_data> image,
                   diagnostics::services destinations =
                       {}); ///< Snapshot pixels into retained GPU staging with rollback on failure.
    ~texture_upload(); ///< Retire native resources through their last GPU fence without waiting.
    texture_upload(const texture_upload&) = delete;            ///< One owner retires the binding.
    texture_upload& operator=(const texture_upload&) = delete; ///< Resource identities are unique.
    bool ready(); ///< Poll upload completion and release decoded pixels when the fence completes.
    texture_binding binding(); ///< Reject use before GPU initialization or after cancellation.
    void cancel(); ///< Invalidate handles and retire staging/resources safely after submission.
  private:
    renderer::resource_store& resources_;     ///< Borrowed graphics coordinator registry.
    std::shared_ptr<const image_data> image_; ///< Retain pixels through upload completion.
    diagnostics::services destinations_;      ///< Inherited optional diagnostics endpoints.
    texture_binding binding_{};               ///< Invalid after cancellation.
    renderer::completion_token completion_{}; ///< CPU job completion is not this GPU fence.
};

/// @brief Bounded process-local cache; pass_renderer must outlive it, all calls on its coordinator.
class pipeline_cache
{
  public:
    explicit pipeline_cache(renderer::pass_renderer& renderer, std::size_t capacity = 32,
                            diagnostics::services destinations =
                                {}); ///< Bound exact-key reuse within one renderer/device lifetime.
    ~pipeline_cache(); ///< Retire all cached pipelines using renderer fence protection.
    pipeline_cache(const pipeline_cache&) = delete; ///< Cached handles have one retirement owner.
    pipeline_cache& operator=(const pipeline_cache&) = delete; ///< Preserve unique cache ownership.
    renderer::pipeline_handle get(const renderer::pipeline_description&
                                      description); ///< Compile once for exact source/entry/state,
                                                    ///< never cache failed construction.
    void clear(); ///< Invalidate cached handles; native objects retire after their last GPU use.
    std::size_t
    size() const; ///< Number of successful unique descriptions retained; coordinator-only.
  private:
    /// @brief One exact pipeline key and its renderer-owned native identity.
    struct entry
    {
        renderer::pipeline_description description; ///< Exact source, entries and render state key.
        renderer::pipeline_handle
            pipeline; ///< Native pipeline owned until cache clear/destruction.
    };

    renderer::pass_renderer& renderer_; ///< Device-local pipeline registry.
    std::size_t capacity_; ///< Hard entry limit; no silent eviction of handles held by callers.
    diagnostics::services destinations_; ///< Optional logging and CPU profiling.
    std::vector<entry> entries_;         ///< Reserved before any GPU pipeline is published.
};
} // namespace atlas::renderer_content
